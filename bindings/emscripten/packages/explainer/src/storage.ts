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

import type { SolidityStorageLayout, ParsedKey, ResolvedSlot, SolidityStorageEntry } from './types.js';
import { keccak256, AbiCoder } from 'ethers';

const MAX_SAFE = BigInt(Number.MAX_SAFE_INTEGER);

/** Convert a bigint slot to number when safe, hex string otherwise. */
function safeBaseSlot(n: bigint): number | string {
    if (n >= -1n && n <= MAX_SAFE) return Number(n);
    return '0x' + n.toString(16).padStart(64, '0');
}

/**
 * Parse a slotSource (KECCAK256 preimage) into its components.
 *
 * For a Solidity `mapping(K => V)` at storage slot `p`, the storage
 * location of key `k` is `keccak256(h(k) . p)`. The slotSource is
 * the preimage `h(k) . p`, i.e. `[padded_key (32 bytes)][base_slot (32 bytes)]`.
 *
 * @param slotSource - Hex-encoded preimage (64 bytes = 128 hex chars + 0x prefix)
 * @return Parsed base slot and raw key data
 */
export function parseSlotSource(slotSource: string): { baseSlot: bigint; keyData: string } {
    const hex = slotSource.startsWith('0x') ? slotSource.slice(2) : slotSource;

    if (hex.length !== 128) {
        return { baseSlot: -1n, keyData: '' };
    }

    const keyData = '0x' + hex.slice(0, 64);
    const baseSlotHex = hex.slice(64, 128);
    const baseSlot = BigInt('0x' + baseSlotHex);

    return { baseSlot, keyData };
}

/**
 * Resolve a storage slot change to a human-readable variable reference
 * using the slotSource preimage and optional Solidity storage layout.
 *
 * Strategy:
 * 1. Parse slotSource into baseSlot + key
 * 2. If storageLayout available: match baseSlot to variable name and key type
 * 3. If not: return parsed info with heuristic key type detection
 *
 * @param slotSource - KECCAK256 preimage from EVM interception
 * @param layout - Solidity compiler storage layout (null if unavailable)
 */
export function resolveStorageSlot(
    slotSource: string,
    layout: SolidityStorageLayout | null,
): ResolvedSlot {
    const { baseSlot, keyData } = parseSlotSource(slotSource);

    if (baseSlot < 0n) {
        return { baseSlot: -1, raw: slotSource };
    }

    const keys = [detectKeyType(keyData)];

    if (layout && layout.types) {
        const entry = layout.storage.find(s => BigInt(s.slot) === baseSlot);
        if (entry) {
            const typeInfo = layout.types[entry.type];
            if (typeInfo) {
                const resolvedKeys = resolveKeysFromType(keys, typeInfo, layout);
                return {
                    variableName: entry.label,
                    variableType: typeInfo.label,
                    keys: resolvedKeys,
                    baseSlot: safeBaseSlot(baseSlot),
                    raw: slotSource,
                };
            }
        }
    }

    return {
        keys,
        baseSlot: safeBaseSlot(baseSlot),
        raw: slotSource,
    };
}

/**
 * Resolve a storage slot that has no slotSource by matching the raw slot
 * number against the storage layout. Handles direct variables, packed
 * variables, and dynamic arrays (via keccak256 heuristic).
 *
 * @param slotHex - The raw slot hash (hex string with 0x prefix)
 * @param layout - Solidity compiler storage layout (null if unavailable)
 */
export function resolveDirectSlot(
    slotHex: string,
    layout: SolidityStorageLayout | null,
): ResolvedSlot {
    const slotBigInt = BigInt(slotHex);

    if (!layout?.storage || !layout.types) {
        return { baseSlot: safeBaseSlot(slotBigInt), raw: slotHex };
    }

    const entry = layout.storage.find(s => BigInt(s.slot) === slotBigInt);
    if (entry) {
        const typeInfo = layout.types[entry.type];
        return {
            variableName: entry.label,
            variableType: typeInfo?.label ?? entry.type,
            baseSlot: safeBaseSlot(slotBigInt),
            raw: slotHex,
        };
    }

    const arrayResult = resolveArraySlot(slotBigInt, layout);
    if (arrayResult) return arrayResult;

    return { baseSlot: safeBaseSlot(slotBigInt), raw: slotHex };
}

/**
 * Heuristic: check if the slot falls inside a dynamic array range.
 * For a dynamic array at slot `p`, elements start at `keccak256(abi.encode(p))`.
 */
function resolveArraySlot(
    slot: bigint,
    layout: SolidityStorageLayout,
): ResolvedSlot | null {
    if (!layout.storage || !layout.types) return null;

    for (const entry of layout.storage) {
        const typeInfo = layout.types[entry.type];
        if (!typeInfo || typeInfo.encoding !== 'dynamic_array') continue;

        const arrayBaseHash = keccak256(
            AbiCoder.defaultAbiCoder().encode(['uint256'], [BigInt(entry.slot)]),
        );
        const arrayStart = BigInt(arrayBaseHash);

        const MAX_ARRAY_ELEMENTS = 100_000;
        if (slot >= arrayStart) {
            const elementSize = getElementSlotSize(typeInfo, layout);
            if (elementSize <= 0) continue;

            const offset = slot - arrayStart;
            const index = Number(offset / BigInt(elementSize));
            if (index > MAX_ARRAY_ELEMENTS) continue;
            const remainder = Number(offset % BigInt(elementSize));

            const structField = (remainder > 0 && typeInfo.base)
                ? resolveStructField(remainder, typeInfo.base, layout)
                : undefined;

            return {
                variableName: entry.label,
                variableType: typeInfo.label,
                baseSlot: safeBaseSlot(BigInt(entry.slot)),
                raw: '0x' + slot.toString(16).padStart(64, '0'),
                arrayIndex: index,
                structField,
            };
        }
    }

    return null;
}

function getElementSlotSize(
    arrayType: { base?: string; numberOfBytes: string },
    layout: SolidityStorageLayout,
): number {
    if (!arrayType.base || !layout.types) return 1;
    const baseType = layout.types[arrayType.base];
    if (!baseType) return 1;
    return Math.ceil(Number(baseType.numberOfBytes) / 32);
}

function resolveStructField(
    slotOffset: number,
    baseType: string,
    layout: SolidityStorageLayout,
): string | undefined {
    if (!layout.types) return undefined;
    const typeInfo = layout.types[baseType];
    if (!typeInfo?.members) return undefined;
    const member = typeInfo.members.find(
        (m: SolidityStorageEntry) => Number(m.slot) === slotOffset,
    );
    return member?.label;
}

/**
 * Heuristic detection of key type from 32-byte padded hex data.
 * Addresses are left-padded with 12 zero bytes and have enough
 * entropy in the lower 20 bytes to not be a small integer.
 */
function detectKeyType(keyData: string): ParsedKey {
    const hex = keyData.startsWith('0x') ? keyData.slice(2) : keyData;

    if (hex.length !== 64) {
        return { type: 'unknown', value: '0x' + hex };
    }

    const leadingZeros = hex.slice(0, 24);
    const addressPart = hex.slice(24);
    if (leadingZeros === '000000000000000000000000') {
        const addrVal = BigInt('0x' + addressPart);
        if (addrVal > 2n ** 32n) {
            return { type: 'address', value: '0x' + addressPart };
        }
        return { type: 'uint256', value: addrVal.toString() };
    }

    return { type: 'bytes32', value: '0x' + hex };
}

/**
 * Refine key types using information from the storage layout.
 * If the layout says the mapping key is `t_address`, we know the key is an address.
 */
function resolveKeysFromType(
    detectedKeys: ParsedKey[],
    typeInfo: { key?: string; label: string },
    layout: SolidityStorageLayout,
): ParsedKey[] {
    if (!typeInfo.key || !layout.types) return detectedKeys;

    const keyTypeInfo = layout.types[typeInfo.key];
    if (!keyTypeInfo) return detectedKeys;

    return detectedKeys.map(key => {
        const hex = key.value.startsWith('0x') ? key.value.slice(2) : key.value;
        if (keyTypeInfo.label === 'address') {
            const addr = hex.length === 40 ? '0x' + hex : '0x' + hex.padStart(40, '0');
            return { type: 'address' as const, value: addr };
        }
        if (keyTypeInfo.label.startsWith('uint')) {
            return { type: 'uint256' as const, value: BigInt('0x' + hex.padStart(64, '0')).toString() };
        }
        return key;
    });
}
