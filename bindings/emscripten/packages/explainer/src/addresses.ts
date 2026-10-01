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
    DecodedCall, DecodedEvent, ResolvedSlot, SimulationResult, TxParams,
} from './types.js';
import { extractPackedValue } from './storage.js';

const ADDRESS_RE = /^0x[a-fA-F0-9]{40}$/;

export interface UsedAddressSources {
    decodedCall?: DecodedCall;
    decodedTrace?: (DecodedCall | null)[];
    decodedEvents?: (DecodedEvent | null)[];
    resolvedStorage?: Map<string, ResolvedSlot[]>;
}

/**
 * Addresses that the prompt actually prints, in first-seen order.
 *
 * The access list is omitted: it can be large and is not shown. The zero
 * address is kept so a mint `Transfer` still gets the `Null Address` label.
 *
 * @param result - Simulation result
 * @param txParams - Original transaction parameters
 * @param extra - Decoded calls, events, and resolved storage slots
 * @return Lowercase addresses, de-duplicated, sender first when present
 */
export function collectUsedAddresses(
    result: SimulationResult,
    txParams: TxParams,
    extra?: UsedAddressSources,
): string[] {
    const order: string[] = [];
    const seen = new Set<string>();
    const add = (address?: string | null): void => {
        if (!address || !ADDRESS_RE.test(address)) return;
        const addr = address.toLowerCase();
        if (seen.has(addr)) return;
        seen.add(addr);
        order.push(addr);
    };
    const addParams = (params?: { type: string; value: string }[]): void => {
        if (!params) return;
        for (const param of params) addTypedValue(add, param.type, param.value);
    };

    add(txParams.from);
    add(txParams.to);

    if (result.trace) {
        for (const frame of result.trace) {
            add(frame.from);
            add(frame.to);
        }
    }

    if (result.logs) {
        for (let i = 0; i < result.logs.length; i++) {
            const log = result.logs[i];
            add(log.raw?.address);
            addParams(log.inputs);
            addParams(extra?.decodedEvents?.[i]?.params);
        }
    }

    addParams(extra?.decodedCall?.params);
    if (extra?.decodedTrace && result.trace) {
        for (let i = 0; i < result.trace.length; i++) addParams(extra.decodedTrace[i]?.params);
    }

    if (result.stateChanges) {
        for (const change of result.stateChanges) add(change.address);
    }

    if (extra?.resolvedStorage) {
        for (const slots of extra.resolvedStorage.values()) {
            for (const slot of slots) {
                if (!slot.keys) continue;
                for (const key of slot.keys) {
                    if (key.type === 'address') add(key.value);
                }
            }
        }
    }

    if (result.stateChanges && extra?.resolvedStorage) {
        for (const change of result.stateChanges) {
            const slots = extra.resolvedStorage.get(change.address.toLowerCase());
            if (!change.storage || !slots) continue;
            for (let i = 0; i < change.storage.length; i++) {
                const members = slots[i]?.members;
                if (!members) continue;
                const wordPrev = change.storage[i].previousValue;
                const wordNext = change.storage[i].newValue;
                for (const member of members) {
                    if (member.variableType !== 'address' || member.numberOfBytes !== 20) continue;
                    addPackedAddress(add, wordPrev, member.offset);
                    addPackedAddress(add, wordNext, member.offset);
                }
            }
        }
    }

    return order;
}

/**
 * Record a 20-byte address packed into a storage word.
 *
 * @param add - Address collector
 * @param word - 32-byte hex storage word
 * @param offset - Byte offset of the address from the low-order end
 */
function addPackedAddress(
    add: (address?: string | null) => void,
    word: string,
    offset: number,
): void {
    const value = extractPackedValue(word, offset, 20);
    if (value === null) return;
    add('0x' + value.toString(16).padStart(40, '0'));
}

/**
 * Record address-typed ABI values, including `address[]` lists rendered as text.
 *
 * @param add - Address collector
 * @param type - ABI type string
 * @param value - Decoded value text
 */
function addTypedValue(
    add: (address?: string | null) => void,
    type: string,
    value: string,
): void {
    if (!type || !type.includes('address') || !value) return;
    if (type.replace(/\s+/g, '') === 'address') {
        add(value);
        return;
    }
    const matches = value.match(/0x[a-fA-F0-9]{40}/g);
    if (!matches) return;
    for (const match of matches) add(match);
}
