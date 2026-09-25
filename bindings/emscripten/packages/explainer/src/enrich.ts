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
    ContractCache, VerifiedContract, AccessListEntry, ContractStateChange,
} from './types.js';
import { fetchCompilationInput } from './sourcify.js';
import { decodeFunctionCall, decodeEventLog, decodeRevertData } from './decoder.js';
import { resolveStorageSlot, resolveDirectSlot } from './storage.js';
import { compileAndVerify } from './compiler.js';
import { extractStorageLayout } from './layout.js';
import { cacheGet, cacheSet, cacheGetLayout, cacheSetLayout, getDefaultCache } from './cache.js';
import { elapsedMs, explainerLog } from './log.js';
import { assignRefIds } from './refs.js';
import { parseExplanation } from './lines.js';

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
    options?: { sourcifyBaseUrl?: string; cache?: ContractCache },
): Promise<EnrichedContext> {
    const cache = options?.cache || await getDefaultCache();
    const { hashes: codeHashes, eoas } = buildCodeHashMap(result.accessList);
    const addresses = collectAddresses(result, txParams);
    explainerLog('info', 'enrich start', {
        scope: 'enrich', chainId, addresses: addresses.length, to: txParams.to,
    });
    const started = Date.now();
    const contracts = await fetchAllContracts(addresses, chainId, codeHashes, eoas, cache, options?.sourcifyBaseUrl);
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

    return { contracts, decodedCall, decodedError, resolvedStorage, decodedTrace, decodedEvents };
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
): Promise<Map<string, ContractMetadata>> {
    // Sequential: parallel parse/compile of Sourcify sources OOMs on large contracts.
    const results: Array<[string, ContractMetadata]> = [];
    for (let i = 0; i < addresses.length; i++) {
        const addr = addresses[i];
        explainerLog('debug', 'resolve contract', {
            scope: 'enrich', index: i + 1, of: addresses.length, address: addr,
        });
        const started = Date.now();
        results.push(await resolveContract(addr, chainId, codeHashes, eoas, cache, baseUrl));
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
): Promise<[string, ContractMetadata]> {
    const addr = address.toLowerCase();
    const empty: ContractMetadata = { abi: null, sources: null, storageLayout: null, contractName: null };

    if (eoas.has(addr)) {
        explainerLog('debug', 'skip EOA', { scope: 'enrich', address: addr });
        return [addr, empty];
    }

    const codeHash = codeHashes.get(addr);
    if (codeHash) {
        const cached = await cacheGet(cache, codeHash);
        if (cached) {
            explainerLog('debug', 'codeHash cache hit', { scope: 'enrich', address: addr });
            return [addr, verifiedToMeta(cached)];
        }

        const existing = inflightByCodeHash.get(codeHash);
        if (existing) return [addr, await existing];

        const promise = resolveFromSourcify(addr, chainId, codeHash, cache, baseUrl, empty)
            .finally(() => {
                if (inflightByCodeHash.get(codeHash) === promise) inflightByCodeHash.delete(codeHash);
            });
        inflightByCodeHash.set(codeHash, promise);
        return [addr, await promise];
    }

    return [addr, await resolveFromSourcify(addr, chainId, undefined, cache, baseUrl, empty)];
}

async function resolveFromSourcify(
    addr: string,
    chainId: number,
    codeHash: string | undefined,
    cache: ContractCache,
    baseUrl: string | undefined,
    empty: ContractMetadata,
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
            );
        } catch {
            explainerLog('warn', 'bytecode verify threw', { scope: 'enrich', address: addr });
            return { abi: comp.abi, sources: comp.sources, storageLayout, contractName: comp.contractName };
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
            };
            await cacheSet(cache, codeHash, verifiedContract);
        }

        return { abi, sources: comp.sources, storageLayout, contractName: comp.contractName };
    }

    return { abi: comp.abi, sources: comp.sources, storageLayout, contractName: comp.contractName };
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
    allowedRefs?: Iterable<string>,
): EnhancedSimulationResult {
    const ids = assignRefIds(result);
    const logs: EnhancedLog[] = (result.logs || []).map((log, i) => {
        const decoded = context.decodedEvents?.[i] ?? undefined;
        return { ...log, id: ids.logIds[i], ...(decoded ? { decoded } : {}) };
    });

    const trace: EnhancedTraceEntry[] | undefined = result.trace?.map((t, i) => {
        const decoded = context.decodedTrace?.[i] ?? undefined;
        return { ...t, id: ids.traceIds[i], ...(decoded ? { decoded } : {}) };
    });

    const stateChanges: EnhancedContractStateChange[] | undefined = result.stateChanges?.map((change, c) => {
        const addr = change.address.toLowerCase();
        const resolvedSlots = context.resolvedStorage?.get(addr);

        const storage = change.storage?.map((s, i) => {
            const resolved = resolvedSlots?.[i];
            return { ...s, id: ids.storageIds[c][i], ...(resolved ? { resolved } : {}) };
        });

        const balance = change.balance && ids.balanceIds[c]
            ? { id: ids.balanceIds[c]!, previousValue: change.balance.previousValue, newValue: change.balance.newValue }
            : undefined;

        return { address: change.address, storage, balance };
    });

    const allowed = allowedRefs ?? [];
    return {
        gasUsed: result.gasUsed,
        status: result.status,
        returnValue: result.returnValue,
        logs,
        stateChanges,
        trace,
        explanation,
        lines: parseExplanation(explanation, allowed),
        labels: Object.values(context.labels?.byAddress ?? {}),
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
        const implAddr = implementations?.get(addr);
        const implLayout = implAddr ? contracts.get(implAddr)?.storageLayout : undefined;
        const layout = implLayout ?? contracts.get(addr)?.storageLayout ?? null;

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
