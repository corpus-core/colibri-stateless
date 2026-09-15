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
    DecodedCall, DecodedEvent, DecodedError, ResolvedSlot,
    EnhancedSimulationResult, EnhancedLog, EnhancedTraceEntry, EnhancedContractStateChange,
    ContractCache, VerifiedContract, AccessListEntry,
} from './types.js';
import { fetchCompilationInput } from './sourcify.js';
import { decodeFunctionCall, decodeEventLog, decodeRevertData } from './decoder.js';
import { resolveStorageSlot, resolveDirectSlot } from './storage.js';
import { compileAndVerify } from './compiler.js';
import { extractStorageLayout } from './layout.js';
import { cacheGet, cacheSet, getDefaultCache } from './cache.js';
import { elapsedMs, explainerLog } from './log.js';

/**
 * Enrich a simulation result with decoded contract metadata.
 *
 * Resolve chain per contract address:
 * 1. Skip EOAs (`EMPTY_CODE_HASH` in `accessList`)
 * 2. Cache lookup by `codeHash` (from `accessList`)
 * 3. Fetch source from Sourcify (cached by chainId+address, including 404 misses),
 *    compile + verify bytecode, extract layout via skeleton
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
    explainerLog('info', 'enrich done', {
        scope: 'enrich',
        ms: elapsedMs(started),
        addresses: addresses.length,
        withAbi: [...contracts.values()].filter(c => !!c.abi).length,
    });
    const decodedCall = decodeMainCall(txParams, contracts);
    const decodedError = decodeRevertError(result, txParams, contracts);
    const decodedTrace = decodeTraceEntries(result.trace, contracts);
    const decodedEvents = decodeEventLogs(result.logs, contracts);
    const resolvedStorage = resolveAllStorage(result, contracts);

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
        try {
            storageLayout = await extractStorageLayout(comp.sources, comp.contractName ?? undefined) ?? null;
        } catch { /* parser or compiler failure -- proceed without layout */ }
        explainerLog('debug', 'storage layout', {
            scope: 'enrich',
            address: addr,
            ms: elapsedMs(layoutStarted),
            ok: !!storageLayout,
            files: Object.keys(comp.sources).length,
        });
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
            return { abi: comp.abi, sources: comp.sources, storageLayout };
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

        return { abi, sources: comp.sources, storageLayout };
    }

    return { abi: comp.abi, sources: comp.sources, storageLayout };
}

function decodeMainCall(
    txParams: TxParams,
    contracts: Map<string, ContractMetadata>,
): DecodedCall | undefined {
    if (!txParams.to || !txParams.data || txParams.data.length < 10) return undefined;

    const meta = contracts.get(txParams.to.toLowerCase());
    if (!meta?.abi) return undefined;

    return decodeFunctionCall(meta.abi, txParams.data) ?? undefined;
}

function decodeRevertError(
    result: SimulationResult,
    txParams: TxParams,
    contracts: Map<string, ContractMetadata>,
): DecodedError | undefined {
    if (result.status === '0x1') return undefined;
    if (!result.returnValue || result.returnValue === '0x') return undefined;

    const abi = txParams.to ? contracts.get(txParams.to.toLowerCase())?.abi ?? undefined : undefined;
    return decodeRevertData(result.returnValue, abi) ?? undefined;
}

function decodeTraceEntries(
    trace: SimulationResult['trace'],
    contracts: Map<string, ContractMetadata>,
): (DecodedCall | null)[] {
    if (!trace) return [];

    return trace.map(t => {
        if (!t.to || !t.input || t.input.length < 10) return null;

        const meta = contracts.get(t.to.toLowerCase());
        if (!meta?.abi) return null;

        return decodeFunctionCall(meta.abi, t.input);
    });
}

function decodeEventLogs(
    logs: SimulationResult['logs'],
    contracts: Map<string, ContractMetadata>,
): (DecodedEvent | null)[] {
    if (!logs) return [];

    return logs.map(log => {
        if (log.name && log.inputs) return null;

        if (!log.raw?.address || !log.raw.topics?.length) return null;

        const meta = contracts.get(log.raw.address.toLowerCase());
        if (!meta?.abi) return null;

        return decodeEventLog(meta.abi, { topics: log.raw.topics, data: log.raw.data });
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

    return {
        gasUsed: result.gasUsed,
        status: result.status,
        returnValue: result.returnValue,
        logs,
        stateChanges,
        trace,
        explanation,
        decodedCall: context.decodedCall,
        error: context.decodedError,
    };
}

function resolveAllStorage(
    result: SimulationResult,
    contracts: Map<string, ContractMetadata>,
): Map<string, ResolvedSlot[]> {
    const resolved = new Map<string, ResolvedSlot[]>();

    if (!result.stateChanges) return resolved;

    for (const change of result.stateChanges) {
        const addr = change.address.toLowerCase();
        const meta = contracts.get(addr);
        const layout = meta?.storageLayout ?? null;

        if (!change.storage) {
            resolved.set(addr, []);
            continue;
        }

        const slots = change.storage.map(s => {
            if (s.slotSource) {
                return resolveStorageSlot(s.slotSource, layout);
            }
            return resolveDirectSlot(s.slot, layout);
        });

        resolved.set(addr, slots);
    }

    return resolved;
}
