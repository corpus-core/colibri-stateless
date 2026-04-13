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

/**
 * Enrich a simulation result with decoded contract metadata.
 *
 * Resolve chain per contract address:
 * 1. Cache lookup by `codeHash` (from `accessList`)
 * 2. Fetch source from Sourcify, compile + verify bytecode, extract layout via skeleton
 * 3. Best-effort Sourcify fallback when no `codeHash` is available
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
    const codeHashes = buildCodeHashMap(result.accessList);
    const addresses = collectAddresses(result, txParams);
    const contracts = await fetchAllContracts(addresses, chainId, codeHashes, cache, options?.sourcifyBaseUrl);
    const decodedCall = decodeMainCall(txParams, contracts);
    const decodedError = decodeRevertError(result, txParams, contracts);
    const decodedTrace = decodeTraceEntries(result.trace, contracts);
    const decodedEvents = decodeEventLogs(result.logs, contracts);
    const resolvedStorage = resolveAllStorage(result, contracts);

    return { contracts, decodedCall, decodedError, resolvedStorage, decodedTrace, decodedEvents };
}

function buildCodeHashMap(accessList?: AccessListEntry[]): Map<string, string> {
    const map = new Map<string, string>();
    if (!accessList) return map;
    for (const entry of accessList) {
        if (entry.address && entry.codeHash) {
            map.set(entry.address.toLowerCase(), entry.codeHash);
        }
    }
    return map;
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
    cache: ContractCache,
    baseUrl?: string,
): Promise<Map<string, ContractMetadata>> {
    const results = await Promise.all(
        addresses.map(addr => resolveContract(addr, chainId, codeHashes, cache, baseUrl)),
    );
    return new Map(results);
}

async function resolveContract(
    address: string,
    chainId: number,
    codeHashes: Map<string, string>,
    cache: ContractCache,
    baseUrl?: string,
): Promise<[string, ContractMetadata]> {
    const addr = address.toLowerCase();
    const codeHash = codeHashes.get(addr);
    const empty: ContractMetadata = { abi: null, sources: null, storageLayout: null };

    // Step 1: Cache lookup by codeHash
    if (codeHash) {
        const cached = await cacheGet(cache, codeHash);
        if (cached) {
            return [addr, {
                abi: cached.abi,
                sources: cached.sources,
                storageLayout: cached.storageLayout,
            }];
        }
    }

    // Step 2 & 3: Fetch from Sourcify
    const comp = await fetchCompilationInput(addr, chainId, baseUrl);
    if (!comp.abi && !comp.sources) return [addr, empty];

    // Extract storage layout via skeleton when sources are available
    let storageLayout = null;
    if (comp.sources) {
        try {
            storageLayout = await extractStorageLayout(comp.sources, comp.contractName ?? undefined) ?? null;
        } catch { /* parser or compiler failure -- proceed without layout */ }
    }

    if (codeHash && comp.sources && comp.stdJsonInput && comp.compilerVersion) {
        // Step 2: Compile + verify bytecode, then cache
        let verification;
        try {
            verification = await compileAndVerify(
                comp.stdJsonInput, comp.compilerVersion, codeHash, comp.sources,
            );
        } catch {
            return [addr, { abi: comp.abi, sources: comp.sources, storageLayout }];
        }

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

        return [addr, { abi, sources: comp.sources, storageLayout }];
    }

    // Step 3: No codeHash or no sources -- best-effort (not cached)
    return [addr, { abi: comp.abi, sources: comp.sources, storageLayout }];
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
