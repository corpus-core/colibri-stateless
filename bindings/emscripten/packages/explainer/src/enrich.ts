/**
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

import type {
    SimulationResult, TxParams, EnrichedContext, ContractMetadata,
    DecodedCall, DecodedEvent, DecodedError, ResolvedSlot, TraceEntry,
    EnhancedSimulationResult, EnhancedLog, EnhancedTraceEntry, EnhancedContractStateChange,
    EnhancedContractStateReads,
    ContractCache, VerifiedContract, AccessListEntry, ContractStateChange,
    EthCallFn, EthGetCodeFn, TokenInfo, CoveredDefinition, ResolvedStateRead, ExecutedPositions,
    ContractSourceMap, SolidityStorageLayout,
} from './types.js';
import { AbiCoder } from 'ethers';
import { fetchCompilationInput } from './sourcify.js';
import { decodeFunctionCall, decodeEventLog, decodeRevertData } from './decoder.js';
import { resolveStorageSlot, resolveDirectSlot } from './storage.js';
import { compileAndVerify } from './compiler.js';
import { extractStorageLayout } from './layout.js';
import { cacheGet, cacheSet, cacheGetLayout, cacheSetLayout, cacheGetToken, cacheSetToken, getDefaultCache, isSafeTokenSymbol, LAYOUT_CACHE_VERSION } from './cache.js';
import { elapsedMs, explainerLog } from './log.js';
import { collectUsedAddresses } from './addresses.js';
import { lookupAddress } from './known_addresses.js';
import { resolvePcs } from './source_map.js';
import { findCoveredDefinitions } from './source_slice.js';

/**
 * Enrich a simulation result with decoded contract metadata.
 *
 * Resolve chain per contract address:
 * 1. Skip EOAs (`EMPTY_CODE_HASH` in `accessList`)
 * 2. Cache lookup by `codeHash` (from `accessList`)
 * 3. Fetch source from Sourcify (cached by chainId+address, including 404 misses),
 *    compile + verify bytecode, extract layout via skeleton (cached by sources fingerprint)
 * 4. Best-effort Sourcify fallback when no `codeHash` is available
 *
 * @param result - Simulation result from C-core
 * @param txParams - Original transaction parameters
 * @param chainId - EVM chain ID for Sourcify lookups
 * @param options - Optional configuration overrides
 * @return Enriched context for prompt building
 */
export async function enrichSimulation(
    result: SimulationResult,
    txParams: TxParams,
    chainId: number,
    options?: {
        sourcifyBaseUrl?: string;
        cache?: ContractCache;
        ethCall?: EthCallFn;
        ethGetCode?: EthGetCodeFn;
    },
): Promise<EnrichedContext> {
    const cache = options?.cache || await getDefaultCache();
    const { hashes: codeHashes, eoas } = buildCodeHashMap(result.accessList);
    const addresses = collectAddresses(result, txParams);
    explainerLog('info', 'enrich start', {
        scope: 'enrich', chainId, addresses: addresses.length, to: txParams.to,
    });
    const started = Date.now();
    const contracts = await fetchAllContracts(
        addresses, chainId, codeHashes, eoas, cache, options?.sourcifyBaseUrl, options?.ethGetCode,
    );
    const implementations = buildProxyImplementations(result.trace);
    explainerLog('info', 'enrich done', {
        scope: 'enrich',
        ms: elapsedMs(started),
        addresses: addresses.length,
        withAbi: [...contracts.values()].filter(c => !!c.abi).length,
        proxies: implementations.size,
    });
    const decodedCall = decodeMainCall(txParams, contracts, result.accessList, implementations);
    const decodedError = decodeRevertError(result, txParams, contracts, implementations);
    const decodedTrace = decodeTraceEntries(result.trace, contracts, result.accessList, implementations);
    const decodedEvents = decodeEventLogs(result.logs, contracts, implementations);
    const resolvedStorage = resolveAllStorage(result, contracts, implementations);
    const resolvedReads = resolveAllReads(result, contracts, implementations);
    await attachSourceMaps(
        result.positions, contracts, codeHashes, chainId, cache, options?.sourcifyBaseUrl, options?.ethGetCode,
    );
    const coveredDefinitions = buildCoveredDefinitions(result.positions, contracts, implementations);
    const tokens = await resolveErc20Tokens(
        collectUsedAddresses(result, txParams, {
            decodedCall, decodedTrace, decodedEvents, resolvedStorage,
        }),
        txParams.from,
        chainId,
        contracts,
        implementations,
        cache,
        options?.ethCall,
    );

    return {
        contracts,
        decodedCall,
        decodedError,
        resolvedStorage,
        resolvedReads,
        decodedTrace,
        decodedEvents,
        implementations,
        tokens,
        coveredDefinitions,
    };
}

/** keccak256("") -- code hash of accounts without bytecode. */
const EMPTY_CODE_HASH = '0xc5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470';

const inflightByCodeHash = new Map<string, Promise<ContractMetadata>>();

function buildCodeHashMap(accessList?: AccessListEntry[]): { hashes: Map<string, string>; eoas: Set<string> } {
    const hashes = new Map<string, string>();
    const eoas = new Set<string>();
    if (!accessList) return { hashes, eoas };
    for (const entry of accessList) {
        if (!entry.address || !entry.codeHash) continue;
        const addr = entry.address.toLowerCase();
        const hash = entry.codeHash.toLowerCase();
        if (hash === EMPTY_CODE_HASH) {
            eoas.add(addr);
            continue;
        }
        hashes.set(addr, hash);
    }
    return { hashes, eoas };
}

function collectAddresses(result: SimulationResult, txParams: TxParams): string[] {
    const set = new Set<string>();

    if (txParams.to) set.add(txParams.to.toLowerCase());

    if (result.stateChanges) {
        for (const sc of result.stateChanges) {
            set.add(sc.address.toLowerCase());
        }
    }

    if (result.trace) {
        for (const t of result.trace) {
            if (t.to) set.add(t.to.toLowerCase());
            if (t.from) set.add(t.from.toLowerCase());
        }
    }

    if (result.logs) {
        for (const log of result.logs) {
            if (log.raw?.address) set.add(log.raw.address.toLowerCase());
        }
    }

    if (result.accessList) {
        for (const entry of result.accessList) {
            if (entry.address) set.add(entry.address.toLowerCase());
        }
    }

    set.delete('0x0000000000000000000000000000000000000000');

    return [...set];
}

async function fetchAllContracts(
    addresses: string[],
    chainId: number,
    codeHashes: Map<string, string>,
    eoas: Set<string>,
    cache: ContractCache,
    baseUrl?: string,
    ethGetCode?: EthGetCodeFn,
): Promise<Map<string, ContractMetadata>> {
    // Sequential: parallel parse/compile of Sourcify sources OOMs on large contracts.
    const results: Array<[string, ContractMetadata]> = [];
    for (let i = 0; i < addresses.length; i++) {
        const addr = addresses[i];
        explainerLog('debug', 'resolve contract', {
            scope: 'enrich', index: i + 1, of: addresses.length, address: addr,
        });
        const started = Date.now();
        results.push(await resolveContract(addr, chainId, codeHashes, eoas, cache, baseUrl, ethGetCode));
        explainerLog('info', 'resolve contract done', {
            scope: 'enrich',
            address: addr,
            index: i + 1,
            of: addresses.length,
            ms: elapsedMs(started),
            abi: !!results[results.length - 1][1].abi,
        });
        // Yield so V8 can GC parser/solc output between contracts.
        await yieldEventLoop();
    }
    return new Map(results);
}

/**
 * Return to the event loop so large compile/parse heaps can be collected
 * between sequential contract resolutions.
 *
 * @return Resolves on the next turn of the event loop
 */
function yieldEventLoop(): Promise<void> {
    return new Promise(resolve => {
        if (typeof setImmediate === 'function') setImmediate(resolve);
        else setTimeout(resolve, 0);
    });
}

/**
 * Rebuild `storageLayout` when the cached entry predates the current extractor.
 *
 * A verified-contract record pins the layout from the run that compiled it.
 * `null` from an older extractor would otherwise keep Kiln slot pointers unnamed.
 *
 * @param cache - Cache backend
 * @param codeHash - keccak256 of deployed runtime bytecode
 * @param cached - Parsed verified-contract entry
 * @return The same entry, with `storageLayout` replaced when the extractor moved
 */
async function refreshCachedLayout(
    cache: ContractCache,
    codeHash: string,
    cached: VerifiedContract,
): Promise<VerifiedContract> {
    if ((cached.layoutVersion ?? 0) >= LAYOUT_CACHE_VERSION) return cached;
    if (!cached.sources || Object.keys(cached.sources).length === 0) return cached;
    let layout: SolidityStorageLayout | null;
    try {
        layout = await extractStorageLayout(cached.sources, cached.contractName || undefined);
    } catch {
        return cached;
    }
    const updated: VerifiedContract = {
        ...cached,
        storageLayout: layout,
        layoutVersion: LAYOUT_CACHE_VERSION,
    };
    await cacheSet(cache, codeHash, updated);
    return updated;
}

function verifiedToMeta(cached: VerifiedContract): ContractMetadata {
    return {
        abi: cached.abi,
        sources: cached.sources,
        storageLayout: cached.storageLayout,
        contractName: cached.contractName || null,
    };
}

async function resolveContract(
    address: string,
    chainId: number,
    codeHashes: Map<string, string>,
    eoas: Set<string>,
    cache: ContractCache,
    baseUrl?: string,
    ethGetCode?: EthGetCodeFn,
): Promise<[string, ContractMetadata]> {
    const addr = address.toLowerCase();
    const empty: ContractMetadata = { abi: null, sources: null, storageLayout: null };

    if (eoas.has(addr)) {
        explainerLog('debug', 'skip EOA', { scope: 'enrich', address: addr });
        return [addr, empty];
    }

    const codeHash = codeHashes.get(addr);
    if (codeHash) {
        const cached = await cacheGet(cache, codeHash);
        if (cached) {
            explainerLog('debug', 'codeHash cache hit', { scope: 'enrich', address: addr });
            const refreshed = await refreshCachedLayout(cache, codeHash, cached);
            return [addr, verifiedToMeta(refreshed)];
        }

        const existing = inflightByCodeHash.get(codeHash);
        if (existing) return [addr, await existing];

        const promise = resolveFromSourcify(addr, chainId, codeHash, cache, baseUrl, empty, ethGetCode)
            .finally(() => {
                if (inflightByCodeHash.get(codeHash) === promise) inflightByCodeHash.delete(codeHash);
            });
        inflightByCodeHash.set(codeHash, promise);
        return [addr, await promise];
    }

    return [addr, await resolveFromSourcify(addr, chainId, undefined, cache, baseUrl, empty, ethGetCode)];
}

async function resolveFromSourcify(
    addr: string,
    chainId: number,
    codeHash: string | undefined,
    cache: ContractCache,
    baseUrl: string | undefined,
    empty: ContractMetadata,
    ethGetCode?: EthGetCodeFn,
): Promise<ContractMetadata> {
    const comp = await fetchCompilationInput(addr, chainId, baseUrl, cache);
    if (!comp.abi && !comp.sources) {
        explainerLog('debug', 'no sourcify source', { scope: 'enrich', address: addr });
        return empty;
    }

    let storageLayout = null;
    if (comp.sources) {
        const layoutStarted = Date.now();
        const cachedLayout = await cacheGetLayout(cache, comp.sources, comp.contractName ?? undefined);
        if (cachedLayout) {
            storageLayout = cachedLayout;
            explainerLog('debug', 'storage layout cache hit', {
                scope: 'enrich', address: addr, ms: elapsedMs(layoutStarted),
            });
        } else {
            try {
                storageLayout = await extractStorageLayout(comp.sources, comp.contractName ?? undefined) ?? null;
            } catch (err) {
                explainerLog('warn', 'storage layout extract failed', {
                    scope: 'enrich',
                    address: addr,
                    error: err instanceof Error ? err.message : String(err),
                });
            }
            if (storageLayout) {
                await cacheSetLayout(cache, comp.sources, comp.contractName ?? undefined, storageLayout);
            }
            explainerLog('debug', 'storage layout', {
                scope: 'enrich',
                address: addr,
                ms: elapsedMs(layoutStarted),
                ok: !!storageLayout,
                files: Object.keys(comp.sources).length,
            });
        }
    }

    if (codeHash && comp.sources && comp.stdJsonInput && comp.compilerVersion) {
        explainerLog('debug', 'bytecode verify start', {
            scope: 'enrich', address: addr, compiler: comp.compilerVersion,
        });
        let verification;
        try {
            verification = await compileAndVerify(
                comp.stdJsonInput, comp.compilerVersion, codeHash, comp.sources,
                ethGetCode ? { fetchOnChainBytecode: () => ethGetCode(addr) } : undefined,
            );
        } catch {
            explainerLog('warn', 'bytecode verify threw', { scope: 'enrich', address: addr });
            return { abi: comp.abi, sources: comp.sources, storageLayout, contractName: comp.contractName ?? null };
        }

        explainerLog('info', 'bytecode verify', {
            scope: 'enrich',
            address: addr,
            compiler: comp.compilerVersion,
            verified: verification.verified,
        });

        const abi = verification.verified ? (verification.abi ?? comp.abi) : comp.abi;

        if (verification.verified) {
            const verifiedContract: VerifiedContract = {
                abi: abi || [],
                storageLayout,
                sources: comp.sources,
                compilerVersion: comp.compilerVersion,
                contractName: comp.contractName || '',
                layoutVersion: LAYOUT_CACHE_VERSION,
            };
            await cacheSet(cache, codeHash, verifiedContract);
        }

        return { abi, sources: comp.sources, storageLayout, contractName: comp.contractName ?? null };
    }

    return { abi: comp.abi, sources: comp.sources, storageLayout, contractName: comp.contractName ?? null };
}

/**
 * True for call kinds that execute foreign bytecode in the current storage
 * context (`DELEGATECALL`, `CALLCODE`).
 *
 * @param type - Trace `type` string
 * @return Whether the frame is delegate-like
 */
function isDelegateLike(type?: string): boolean {
    if (!type) return false;
    const t = type.toUpperCase();
    return t === 'DELEGATECALL' || t === 'CALLCODE';
}

/**
 * Normalize a single `traceAddress` index from JSON (number or hex string).
 *
 * @param value - Index as number, decimal string, or `0x…` hex
 * @return Non-negative integer index, or `-1` if unparseable
 */
function normalizeTraceIndex(value: number | string): number {
    if (typeof value === 'number' && Number.isFinite(value) && value >= 0) return value;
    const raw = String(value).trim();
    const n = raw.startsWith('0x') || raw.startsWith('0X') ? parseInt(raw, 16) : Number(raw);
    return Number.isFinite(n) && n >= 0 ? n : -1;
}

/**
 * Canonical path key for a trace entry (`""` for the root, `"0/1"` for nested).
 *
 * @param traceAddress - Child-index path from the simulation JSON
 * @return Slash-joined index string
 */
function tracePathKey(traceAddress?: Array<number | string>): string {
    if (!traceAddress?.length) return '';
    return traceAddress.map(normalizeTraceIndex).join('/');
}

/**
 * Map each proxy (parent CALL `to`) to the implementation it DELEGATECALLs.
 *
 * A DELEGATECALL's `from` is msg.sender and `to` is the code address, so the
 * proxy is the parent frame's `to`, found via `traceAddress`. Nested
 * DELEGATECALLs (libraries) do not overwrite the first mapping.
 *
 * @param trace - Simulation call trace
 * @return Lowercase proxy address → lowercase implementation address
 */
export function buildProxyImplementations(trace?: TraceEntry[]): Map<string, string> {
    const map = new Map<string, string>();
    if (!trace?.length) return map;

    const byPath = new Map<string, TraceEntry>();
    for (const entry of trace) {
        byPath.set(tracePathKey(entry.traceAddress), entry);
    }

    for (const entry of trace) {
        if (!isDelegateLike(entry.type) || !entry.to) continue;
        const path = entry.traceAddress ?? [];
        if (!path.length) continue;

        const parentKey = tracePathKey(path.slice(0, -1));
        const parent = byPath.get(parentKey);
        if (!parent?.to || isDelegateLike(parent.type)) continue;

        const proxy = parent.to.toLowerCase();
        const impl = entry.to.toLowerCase();
        if (proxy === impl || map.has(proxy)) continue;
        map.set(proxy, impl);
    }
    return map;
}

/**
 * Decode calldata against a preferred address, then its DELEGATECALL
 * implementation, then every access-list ABI.
 *
 * @param data - Calldata (selector + ABI-encoded args)
 * @param preferredAddress - Address to try first (`tx.to` / trace `to`)
 * @param contracts - Resolved metadata keyed by lowercase address
 * @param accessList - Simulation access list (unstructured fallback)
 * @param implementations - Proxy → implementation from the trace
 * @return Decoded call, or `null` if no ABI recognized the selector
 */
function decodeCallWithFallback(
    data: string,
    preferredAddress: string | undefined,
    contracts: Map<string, ContractMetadata>,
    accessList?: AccessListEntry[],
    implementations?: Map<string, string>,
): DecodedCall | null {
    const tried = new Set<string>();

    const tryAddress = (address?: string): DecodedCall | null => {
        if (!address) return null;
        const addr = address.toLowerCase();
        if (tried.has(addr)) return null;
        tried.add(addr);
        const abi = contracts.get(addr)?.abi;
        if (!abi) return null;
        return decodeFunctionCall(abi, data);
    };

    const preferred = tryAddress(preferredAddress);
    if (preferred) return preferred;

    if (preferredAddress && implementations) {
        const mapped = tryAddress(implementations.get(preferredAddress.toLowerCase()));
        if (mapped) return mapped;
    }

    if (accessList) {
        for (const entry of accessList) {
            const decoded = tryAddress(entry.address);
            if (decoded) return decoded;
        }
    }
    return null;
}

/**
 * Decode the top-level transaction call. Tries `txParams.to`, then the
 * DELEGATECALL implementation, then access-list ABIs.
 *
 * @param txParams - Original transaction parameters
 * @param contracts - Resolved contract metadata
 * @param accessList - Simulation access list for unstructured fallback
 * @param implementations - Proxy → implementation from the trace
 * @return Decoded call, or `undefined` if no ABI matched
 */
function decodeMainCall(
    txParams: TxParams,
    contracts: Map<string, ContractMetadata>,
    accessList?: AccessListEntry[],
    implementations?: Map<string, string>,
): DecodedCall | undefined {
    if (!txParams.data || txParams.data.length < 10) return undefined;
    return decodeCallWithFallback(txParams.data, txParams.to, contracts, accessList, implementations) ?? undefined;
}

/**
 * Decode a revert payload. Tries `txParams.to`, then the mapped implementation.
 *
 * @param result - Simulation result
 * @param txParams - Original transaction parameters
 * @param contracts - Resolved contract metadata
 * @param implementations - Proxy → implementation from the trace
 * @return Decoded error, or `undefined` on success / unknown payload
 */
function decodeRevertError(
    result: SimulationResult,
    txParams: TxParams,
    contracts: Map<string, ContractMetadata>,
    implementations?: Map<string, string>,
): DecodedError | undefined {
    if (result.status === '0x1') return undefined;
    if (!result.returnValue || result.returnValue === '0x') return undefined;

    const tryAbi = (addr?: string): DecodedError | undefined => {
        if (!addr) return undefined;
        const abi = contracts.get(addr.toLowerCase())?.abi ?? undefined;
        return decodeRevertData(result.returnValue, abi) ?? undefined;
    };

    const fromTo = tryAbi(txParams.to);
    if (fromTo) return fromTo;
    if (txParams.to && implementations) {
        const fromImpl = tryAbi(implementations.get(txParams.to.toLowerCase()));
        if (fromImpl) return fromImpl;
    }
    return decodeRevertData(result.returnValue, undefined) ?? undefined;
}

/**
 * Decode each trace entry. Prefers the frame `to`, then a mapped implementation,
 * then access-list ABIs.
 *
 * @param trace - Simulation call trace
 * @param contracts - Resolved contract metadata
 * @param accessList - Simulation access list for unstructured fallback
 * @param implementations - Proxy → implementation from the trace
 * @return Per-entry decoded call or `null`
 */
function decodeTraceEntries(
    trace: SimulationResult['trace'],
    contracts: Map<string, ContractMetadata>,
    accessList?: AccessListEntry[],
    implementations?: Map<string, string>,
): (DecodedCall | null)[] {
    if (!trace) return [];

    return trace.map(t => {
        if (!t.input || t.input.length < 10) return null;
        return decodeCallWithFallback(t.input, t.to, contracts, accessList, implementations);
    });
}

/**
 * Decode a log against a preferred address, then its DELEGATECALL
 * implementation, then every fetched contract ABI.
 *
 * @param log - Raw log topics and data
 * @param preferredAddress - Emitter address (`log.raw.address`, the proxy)
 * @param contracts - Resolved metadata keyed by lowercase address
 * @param implementations - Proxy → implementation from the trace
 * @return Decoded event, or `null` if no ABI recognized the topic
 */
function decodeEventWithFallback(
    log: { topics: string[]; data: string },
    preferredAddress: string | undefined,
    contracts: Map<string, ContractMetadata>,
    implementations?: Map<string, string>,
): DecodedEvent | null {
    const tried = new Set<string>();

    const tryAddress = (address?: string): DecodedEvent | null => {
        if (!address) return null;
        const addr = address.toLowerCase();
        if (tried.has(addr)) return null;
        tried.add(addr);
        const abi = contracts.get(addr)?.abi;
        if (!abi) return null;
        return decodeEventLog(abi, log);
    };

    const preferred = tryAddress(preferredAddress);
    if (preferred) return preferred;

    if (preferredAddress && implementations) {
        const mapped = tryAddress(implementations.get(preferredAddress.toLowerCase()));
        if (mapped) return mapped;
    }

    for (const addr of contracts.keys()) {
        const decoded = tryAddress(addr);
        if (decoded) return decoded;
    }
    return null;
}

/**
 * Decode each log. Prefers `raw.address`, then a mapped implementation ABI,
 * then every fetched contract ABI. Logs already named by the C-core are skipped.
 *
 * @param logs - Simulation logs
 * @param contracts - Resolved contract metadata
 * @param implementations - Proxy → implementation from the trace
 * @return Per-log decoded event or `null`
 */
function decodeEventLogs(
    logs: SimulationResult['logs'],
    contracts: Map<string, ContractMetadata>,
    implementations?: Map<string, string>,
): (DecodedEvent | null)[] {
    if (!logs) return [];

    return logs.map(log => {
        if (log.name && log.inputs) return null;
        if (!log.raw?.topics?.length) return null;

        return decodeEventWithFallback(
            { topics: log.raw.topics, data: log.raw.data ?? '0x' },
            log.raw.address,
            contracts,
            implementations,
        );
    });
}

/**
 * Merge the original simulation result with enriched context and explanation
 * into a single JSON-serializable object suitable for UI consumption.
 */
export function toEnhancedResult(
    result: SimulationResult,
    context: EnrichedContext,
    explanation: string,
): EnhancedSimulationResult {
    const logs: EnhancedLog[] = (result.logs || []).map((log, i) => {
        const decoded = context.decodedEvents?.[i] ?? undefined;
        return decoded ? { ...log, decoded } : { ...log };
    });

    const trace: EnhancedTraceEntry[] | undefined = result.trace?.map((t, i) => {
        const decoded = context.decodedTrace?.[i] ?? undefined;
        return decoded ? { ...t, decoded } : { ...t };
    });

    const stateChanges: EnhancedContractStateChange[] | undefined = result.stateChanges?.map(change => {
        const addr = change.address.toLowerCase();
        const resolvedSlots = context.resolvedStorage?.get(addr);

        const storage = change.storage?.map((s, i) => {
            const resolved = resolvedSlots?.[i];
            return resolved ? { ...s, resolved } : { ...s };
        });

        return { address: change.address, storage, balance: change.balance };
    });

    let stateReads: EnhancedContractStateReads[] | undefined;
    if (context.resolvedReads && context.resolvedReads.size > 0) {
        stateReads = [];
        for (const [address, reads] of context.resolvedReads) {
            if (!reads.length) continue;
            stateReads.push({ address, reads });
        }
        if (!stateReads.length) stateReads = undefined;
    }

    return {
        gasUsed: result.gasUsed,
        status: result.status,
        returnValue: result.returnValue,
        logs,
        stateChanges,
        stateReads,
        trace,
        explanation,
        decodedCall: context.decodedCall,
        error: context.decodedError,
    };
}

function resolveAllStorage(
    result: SimulationResult,
    contracts: Map<string, ContractMetadata>,
    implementations?: Map<string, string>,
): Map<string, ResolvedSlot[]> {
    const resolved = new Map<string, ResolvedSlot[]>();

    if (!result.stateChanges) return resolved;

    for (const change of result.stateChanges) {
        const addr = change.address.toLowerCase();
        // Delegated storage lives in the implementation. The proxy contract's
        // own layout (ERC-1967 admin slots) does not describe those variables.
        const implAddr = implementations?.get(addr);
        const layout = implAddr
            ? (contracts.get(implAddr)?.storageLayout ?? null)
            : (contracts.get(addr)?.storageLayout ?? null);

        if (!change.storage) {
            resolved.set(addr, []);
            continue;
        }

        const candidates = collectMappingKeyCandidates(result, change);
        const slots = change.storage.map(s => {
            if (s.slotSource) {
                return resolveStorageSlot(s.slotSource, layout, candidates);
            }
            return resolveDirectSlot(s.slot, layout);
        });

        resolved.set(addr, slots);
    }

    return resolved;
}

/**
 * Resolve `accessList[].storage` entries the same way `resolveAllStorage`
 * treats `stateChanges`, skipping slots that also appear in `stateChanges`
 * (writes trump reads — a written slot is already shown under "State Changes").
 *
 * Returns an empty map when the simulation was not asked for `state_values`.
 *
 * @param result - Simulation result
 * @param contracts - Resolved metadata
 * @param implementations - Proxy → implementation
 * @return Lowercase address → per-slot resolutions
 */
function resolveAllReads(
    result: SimulationResult,
    contracts: Map<string, ContractMetadata>,
    implementations?: Map<string, string>,
): Map<string, ResolvedStateRead[]> {
    const resolved = new Map<string, ResolvedStateRead[]>();
    if (!result.accessList?.length) return resolved;

    const writtenSlots = collectWrittenSlots(result.stateChanges);
    const globalCandidates = collectGlobalMappingKeyCandidates(result);

    for (const entry of result.accessList) {
        if (!entry.address || !entry.storage?.length) continue;
        const addr = entry.address.toLowerCase();
        const written = writtenSlots.get(addr);
        const implAddr = implementations?.get(addr);
        const layout = implAddr
            ? (contracts.get(implAddr)?.storageLayout ?? null)
            : (contracts.get(addr)?.storageLayout ?? null);

        const candidates = [addr, ...globalCandidates];
        const reads: ResolvedStateRead[] = [];
        for (const slot of entry.storage) {
            if (!slot || typeof slot.slot !== 'string') continue;
            if (written && written.has(slot.slot.toLowerCase())) continue;
            const resolvedSlot = slot.slotSource
                ? resolveStorageSlot(slot.slotSource, layout, candidates)
                : resolveDirectSlot(slot.slot, layout);
            reads.push({ slot: slot.slot, value: slot.value, resolved: resolvedSlot });
        }
        if (reads.length) resolved.set(addr, reads);
    }
    return resolved;
}

/**
 * Index every written storage slot by contract so read-only slots can be
 * subtracted from the access list.
 *
 * @param changes - `SimulationResult.stateChanges`
 * @return Lowercase address → set of written raw slot keys (lowercase)
 */
function collectWrittenSlots(
    changes: SimulationResult['stateChanges'],
): Map<string, Set<string>> {
    const out = new Map<string, Set<string>>();
    if (!changes) return out;
    for (const change of changes) {
        if (!change.storage?.length) continue;
        const addr = change.address.toLowerCase();
        let set = out.get(addr);
        if (!set) { set = new Set<string>(); out.set(addr, set); }
        for (const slot of change.storage) {
            if (typeof slot.slot === 'string') set.add(slot.slot.toLowerCase());
        }
    }
    return out;
}

/**
 * Address candidates for mapping-key detection, computed once per tx from
 * logs, trace, and access list.
 *
 * @param result - Simulation result
 * @return Deduplicated lowercase addresses
 */
function collectGlobalMappingKeyCandidates(result: SimulationResult): string[] {
    const keys = new Set<string>();
    const add = (value?: string): void => {
        if (!value) return;
        const v = value.toLowerCase();
        if (ADDRESS_CANDIDATE_RE.test(v)) keys.add(v);
    };
    for (const log of result.logs ?? []) {
        add(log.raw?.address);
        for (const input of log.inputs ?? []) {
            if (input.type === 'address') add(input.value);
        }
        for (const topic of (log.raw?.topics ?? []).slice(1)) {
            if (INDEXED_ADDRESS_TOPIC_RE.test(topic)) add('0x' + topic.slice(26));
        }
    }
    for (const t of result.trace ?? []) {
        add(t.from);
        add(t.to);
    }
    for (const entry of result.accessList ?? []) {
        add(entry.address);
    }
    return [...keys];
}

/**
 * Recompile every contract referenced by `positions` a second time with
 * `evm.deployedBytecode.sourceMap` requested, and attach the result to the
 * matching `ContractMetadata`. Contracts without cached compilation input or
 * without sources are left untouched.
 *
 * @param positions - `SimulationResult.positions`
 * @param contracts - Resolved metadata (mutated in place)
 * @param chainId - EVM chain ID (for the compilation-input cache)
 * @param cache - Persistent cache backend
 * @param baseUrl - Optional Sourcify override
 */
async function attachSourceMaps(
    positions: ExecutedPositions[] | undefined,
    contracts: Map<string, ContractMetadata>,
    codeHashes: Map<string, string>,
    chainId: number,
    cache: ContractCache,
    baseUrl?: string,
    ethGetCode?: EthGetCodeFn,
): Promise<void> {
    if (!positions?.length) return;
    const seen = new Set<string>();
    for (const pos of positions) {
        if (!pos?.address) continue;
        const addr = pos.address.toLowerCase();
        if (seen.has(addr)) continue;
        seen.add(addr);
        const meta = contracts.get(addr);
        if (!meta || meta.sourceMap || !meta.sources) continue;

        // Without a codeHash the second solc pass has nothing to verify
        // against — the first pass ran with the same input, so the second
        // one either produces the same runtime bytecode or the toolchain is
        // non-deterministic and we should not attribute source ranges either.
        const codeHash = codeHashes.get(addr);
        if (!codeHash) continue;

        const comp = await fetchCompilationInput(addr, chainId, baseUrl, cache);
        if (!comp.stdJsonInput || !comp.compilerVersion) continue;

        try {
            const started = Date.now();
            const verification = await compileAndVerify(
                comp.stdJsonInput, comp.compilerVersion, codeHash, meta.sources,
                {
                    includeSourceMap: true,
                    ...(ethGetCode ? { fetchOnChainBytecode: () => ethGetCode(addr) } : {}),
                },
            );
            explainerLog('info', 'source-map compile', {
                scope: 'positions',
                address: addr,
                ms: elapsedMs(started),
                verified: verification.verified,
                mapped: !!verification.sourceMap,
            });
            if (verification.verified && verification.sourceMap) {
                meta.sourceMap = {
                    sourceMap: verification.sourceMap.sourceMap,
                    runtimeBytecode: verification.sourceMap.runtimeBytecode,
                    sourceIndex: verification.sourceMap.sourceIndex,
                };
            }
        } catch (err) {
            explainerLog('warn', 'source-map compile threw', {
                scope: 'positions',
                address: addr,
                error: err instanceof Error ? err.message : String(err),
            });
        }
    }
}

/**
 * Map each contract with a source-map to the set of Solidity functions and
 * modifiers whose parser range covers at least one executed JUMPDEST.
 *
 * @param positions - `SimulationResult.positions`
 * @param contracts - Resolved metadata (must already carry `sourceMap` where possible)
 * @param implementations - Proxy → implementation
 * @return Lowercase address → covered definitions
 */
function buildCoveredDefinitions(
    positions: ExecutedPositions[] | undefined,
    contracts: Map<string, ContractMetadata>,
    implementations?: Map<string, string>,
): Map<string, CoveredDefinition[]> {
    const out = new Map<string, CoveredDefinition[]>();
    if (!positions?.length) return out;

    for (const pos of positions) {
        if (!pos?.address || !Array.isArray(pos.pcs) || pos.pcs.length === 0) continue;
        const addr = pos.address.toLowerCase();
        // A proxy's positions belong to the implementation, but the C-core
        // already records the *code* address for every frame, so `addr` is
        // the correct lookup key without proxy chasing.
        const meta = contracts.get(addr);
        if (!meta || !meta.sourceMap || !meta.sources) continue;

        const hits = resolvePcs(pos.pcs, meta.sourceMap as ContractSourceMap);
        if (!hits.length) continue;

        const covered = findCoveredDefinitions({
            sources: meta.sources,
            hits: hits.map(h => ({ filename: h.filename, offset: h.start })),
            includeFile: isEmbeddableCoverageSource,
        });
        if (covered.length) out.set(addr, covered);
    }
    // Silence the lint about unused `implementations`; it is kept in the
    // signature because proxy handling may need it in a future revision.
    void implementations;
    return out;
}

/**
 * Coverage-only source filter. Mirrors `isEmbeddableSource` in `prompt.ts`
 * but is duplicated here so `enrich.ts` does not import from the prompt path.
 *
 * @param filename - Source filename
 * @param content - Raw file text
 * @return `true` when the file looks like Solidity source
 */
function isEmbeddableCoverageSource(filename: string, content: string): boolean {
    const name = String(filename ?? '').toLowerCase();
    if (name.endsWith('.yul') || name.endsWith('.bin') || name.endsWith('.abi')) return false;
    const head = String(content ?? '').slice(0, 2000);
    if (/(^|\n)\s*object\s+["'][^"']+["']\s*\{/.test(head)) return false;
    if (!name || name.endsWith('.sol')) return true;
    return false;
}

const ADDRESS_CANDIDATE_RE = /^0x[0-9a-fA-F]{40}$/;
const INDEXED_ADDRESS_TOPIC_RE = /^0x0{24}[0-9a-fA-F]{40}$/;

/**
 * Collect addresses that may be outer keys of a nested mapping
 * (`allowances[owner][spender]`). The intercepted `slotSource` only contains
 * the inner keccak preimage.
 *
 * @param result - Full simulation result
 * @param change - State change currently being resolved
 * @return Deduplicated address candidates (lowercase)
 */
function collectMappingKeyCandidates(
    result: SimulationResult,
    change: ContractStateChange,
): string[] {
    const keys = new Set<string>();
    const add = (value?: string): void => {
        if (!value) return;
        const v = value.toLowerCase();
        if (ADDRESS_CANDIDATE_RE.test(v)) keys.add(v);
    };

    add(change.address);
    for (const log of result.logs ?? []) {
        add(log.raw?.address);
        for (const input of log.inputs ?? []) {
            if (input.type === 'address') add(input.value);
        }
        for (const topic of (log.raw?.topics ?? []).slice(1)) {
            if (INDEXED_ADDRESS_TOPIC_RE.test(topic)) add('0x' + topic.slice(26));
        }
    }
    for (const t of result.trace ?? []) {
        add(t.from);
        add(t.to);
    }
    return [...keys];
}

const SYMBOL_CALL = '0x95d89b41';
const DECIMALS_CALL = '0x313ce567';

interface Erc20Abi {
    symbol: 'string' | 'bytes32';
}

/**
 * Resolve ERC-20 symbol and decimals for addresses the prompt will name.
 *
 * Known addresses that already carry both fields are skipped. The transaction
 * sender is skipped because the prompt always calls that address `sender`.
 * A cache hit skips `ethCall`. Only a validated pair is written back.
 *
 * @param addresses - Lowercase addresses used in the prompt
 * @param sender - `tx.from`, when present
 * @param chainId - EVM chain ID
 * @param contracts - Resolved metadata
 * @param implementations - Proxy → implementation
 * @param cache - Persistent cache
 * @param ethCall - Host callback, optional
 * @return Token info keyed by lowercase address
 */
async function resolveErc20Tokens(
    addresses: string[],
    sender: string | undefined,
    chainId: number,
    contracts: Map<string, ContractMetadata>,
    implementations: Map<string, string>,
    cache: ContractCache,
    ethCall?: EthCallFn,
): Promise<Map<string, TokenInfo>> {
    const tokens = new Map<string, TokenInfo>();
    const senderAddr = sender?.toLowerCase();

    for (const addr of addresses) {
        if (addr === senderAddr) continue;
        const known = lookupAddress(addr);
        if (known?.symbol && known.decimals != null) continue;
        const shape = erc20Abi(addr, contracts, implementations);
        if (!shape) continue;

        const cached = await cacheGetToken(cache, chainId, addr);
        if (cached) {
            tokens.set(addr, cached);
            continue;
        }
        if (!ethCall) continue;

        const resolved = await readErc20(ethCall, addr, shape);
        if (!resolved) continue;
        tokens.set(addr, resolved);
        await cacheSetToken(cache, chainId, addr, resolved);
    }

    return tokens;
}

/**
 * `symbol()` / `decimals()` shape when both view functions take no arguments.
 *
 * The proxy ABI is tried first. When it is not an ERC-20 surface, the
 * implementation ABI is used. The call itself still goes to the proxy so
 * `symbol()` reads the proxy's storage.
 *
 * @param address - Lowercase address
 * @param contracts - Resolved metadata
 * @param implementations - Proxy → implementation
 * @return Symbol return type, or `null` when the ABI is not an ERC-20
 */
function erc20Abi(
    address: string,
    contracts: Map<string, ContractMetadata>,
    implementations: Map<string, string>,
): Erc20Abi | null {
    const own = erc20Shape(contracts.get(address)?.abi);
    if (own) return own;
    const impl = implementations.get(address);
    if (!impl) return null;
    return erc20Shape(contracts.get(impl)?.abi);
}

/**
 * Detect parameterless `symbol()` and `decimals()` on an ABI.
 *
 * @param abi - Contract ABI, possibly missing
 * @return Symbol return type, or `null`
 */
function erc20Shape(abi: unknown[] | null | undefined): Erc20Abi | null {
    if (!Array.isArray(abi)) return null;
    const symbol = viewReturn(abi, 'symbol');
    const decimals = viewReturn(abi, 'decimals');
    if ((symbol !== 'string' && symbol !== 'bytes32') || (decimals !== 'uint8' && decimals !== 'uint256')) {
        return null;
    }
    return { symbol };
}

/**
 * Return type of a no-argument function, if the ABI declares one.
 *
 * @param abi - Contract ABI
 * @param name - Function name
 * @return ABI output type, or `null`
 */
function viewReturn(abi: unknown[], name: string): string | null {
    for (const item of abi) {
        if (!item || typeof item !== 'object') continue;
        const fn = item as Record<string, unknown>;
        if (fn.type !== 'function' || fn.name !== name) continue;
        if (Array.isArray(fn.inputs) && fn.inputs.length > 0) continue;
        if (!Array.isArray(fn.outputs) || fn.outputs.length < 1) continue;
        const out = fn.outputs[0] as Record<string, unknown> | undefined;
        if (typeof out?.type === 'string') return out.type;
    }
    return null;
}

/**
 * Call `symbol()` and `decimals()`. A failure of either drops the token.
 *
 * @param ethCall - Host callback
 * @param address - Token address
 * @param shape - Which symbol encoding the ABI declares
 * @return Validated token info, or `null`
 */
async function readErc20(ethCall: EthCallFn, address: string, shape: Erc20Abi): Promise<TokenInfo | null> {
    const symbolData = await callContract(ethCall, address, SYMBOL_CALL);
    const decimalsData = await callContract(ethCall, address, DECIMALS_CALL);
    // A 32-character symbol ABI-encodes to 96 bytes; bytes32 and decimals are
    // 32. Anything larger is not a symbol we would keep, and decoding it
    // would let a contract spend arbitrary memory in the client.
    if (!symbolData || !decimalsData || !fitsReturn(symbolData, 128) || !fitsReturn(decimalsData, 32)) return null;
    const symbol = shape.symbol === 'bytes32' ? decodeBytes32(symbolData) : decodeAbiString(symbolData);
    const decimals = decodeDecimals(decimalsData);
    if (!symbol || decimals == null || !isSafeTokenSymbol(symbol)) return null;
    return { symbol, decimals };
}

/**
 * Invoke `ethCall` and normalize a hex return value.
 *
 * @param ethCall - Host callback
 * @param to - Contract address
 * @param data - Calldata
 * @return Hex return data, or `null` on failure or empty data
 */
async function callContract(ethCall: EthCallFn, to: string, data: string): Promise<string | null> {
    try {
        const out = await ethCall(to, data);
        if (typeof out !== 'string') return null;
        const hex = out.startsWith('0x') || out.startsWith('0X') ? out : `0x${out}`;
        if (hex === '0x' || hex === '0X') return null;
        return hex;
    } catch (err) {
        explainerLog('debug', 'eth_call failed', {
            scope: 'enrich',
            address: to,
            error: err instanceof Error ? err.message : String(err),
        });
        return null;
    }
}

/**
 * `true` when hex return data is at most `maxBytes` long.
 *
 * @param data - Hex return data
 * @param maxBytes - Maximum decoded byte length
 * @return Whether the payload is small enough to decode
 */
function fitsReturn(data: string, maxBytes: number): boolean {
    const hex = data.replace(/^0x/i, '');
    if (hex.length % 2 !== 0) return false;
    return hex.length / 2 <= maxBytes;
}

/**
 * Decode an ABI `string` return.
 *
 * @param data - Hex return data
 * @return Decoded string, or `null`
 */
function decodeAbiString(data: string): string | null {
    try {
        const [value] = AbiCoder.defaultAbiCoder().decode(['string'], data);
        return typeof value === 'string' ? value : null;
    } catch {
        return null;
    }
}

/**
 * Decode a `bytes32` symbol, stopping at the first zero byte.
 *
 * Non-ASCII bytes are rejected. The prompt sanitizer would drop them anyway,
 * and a partial symbol would be misleading.
 *
 * @param data - Hex return data
 * @return ASCII symbol, or `null`
 */
function decodeBytes32(data: string): string | null {
    try {
        const [value] = AbiCoder.defaultAbiCoder().decode(['bytes32'], data);
        if (typeof value !== 'string') return null;
        const hex = value.replace(/^0x/i, '');
        let out = '';
        for (let i = 0; i < hex.length; i += 2) {
            const byte = parseInt(hex.slice(i, i + 2), 16);
            if (!byte) break;
            if (byte < 32 || byte > 126) return null;
            out += String.fromCharCode(byte);
        }
        return out || null;
    } catch {
        return null;
    }
}

/**
 * Decode `decimals()` as an integer in `0..255`.
 *
 * Both `uint8` and `uint256` ABI returns are a single 32-byte word.
 *
 * @param data - Hex return data
 * @return Decimals, or `null` when the word is out of range
 */
function decodeDecimals(data: string): number | null {
    try {
        const [value] = AbiCoder.defaultAbiCoder().decode(['uint256'], data);
        if (typeof value !== 'bigint' || value < 0n || value > 255n) return null;
        return Number(value);
    } catch {
        return null;
    }
}
