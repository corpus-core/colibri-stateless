/**
 * Copyright (c) 2025 corpus.core
 *
 * SPDX-License-Identifier: MIT
 */

import type { SimulationResult } from './types.js';

/**
 * Stable ids for one simulation result.
 * Assigned from the original order, before the prompt drops or truncates entries.
 */
export interface RefAssignment {
    /** Parallel to `result.logs`. */
    logIds: string[];
    /** Parallel to `result.trace`. */
    traceIds: string[];
    /** `storageIds[contractIndex][slotIndex]`. */
    storageIds: string[][];
    /** Parallel to `result.stateChanges`. Missing when that contract has no balance change. */
    balanceIds: (string | undefined)[];
}

/**
 * Assign 1-based reference ids.
 *
 * `c` calls, `l` logs, `s` storage slots flattened across contracts, `b` ETH balances.
 * The same result always yields the same ids.
 *
 * @param result - Simulation result
 * @return Ids aligned with the result arrays
 */
export function assignRefIds(result: SimulationResult): RefAssignment {
    const logIds = (result.logs ?? []).map((_, i) => `l${i + 1}`);
    const traceIds = (result.trace ?? []).map((_, i) => `c${i + 1}`);
    const storageIds: string[][] = [];
    const balanceIds: (string | undefined)[] = [];
    let storage = 0;
    let balance = 0;
    for (const change of result.stateChanges ?? []) {
        const slots: string[] = [];
        for (let i = 0; i < (change.storage?.length ?? 0); i++) {
            storage += 1;
            slots.push(`s${storage}`);
        }
        storageIds.push(slots);
        if (change.balance) {
            balance += 1;
            balanceIds.push(`b${balance}`);
        } else {
            balanceIds.push(undefined);
        }
    }
    return { logIds, traceIds, storageIds, balanceIds };
}

const REF_RE = /\[([clsb]\d+)\]/g;

/**
 * Ids that actually appear in the user prompt.
 *
 * Entries hidden by filtering keep their id on the enhanced result but are
 * absent here, so a decoding grammar cannot cite them.
 *
 * @param userPrompt - Prompt text produced by `buildPrompt`
 * @return First-seen ids, in order
 */
export function promptRefs(userPrompt: string): string[] {
    const out: string[] = [];
    const seen = new Set<string>();
    for (const match of userPrompt.matchAll(REF_RE)) {
        const id = match[1];
        if (!seen.has(id)) {
            seen.add(id);
            out.push(id);
        }
    }
    return out;
}
