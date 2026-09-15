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
 * @param candidateKeys - Addresses (and hex keys) to try as the *outer* key of
 *   a nested mapping. The intercepted preimage is only one keccak level, so
 *   `allowances[owner][spender]` arrives as `spender || keccak(owner . slot)`.
 */
export function resolveStorageSlot(
    slotSource: string,
    layout: SolidityStorageLayout | null,
    candidateKeys?: string[],
): ResolvedSlot {
    const { baseSlot, keyData } = parseSlotSource(slotSource);

    if (baseSlot < 0n) {
        return { baseSlot: -1, raw: slotSource };
    }

    const keys = [detectKeyType(keyData)];

    if (layout && layout.types) {
        const entry = layout.storage.find(s => {
            const n = parseLayoutSlot(s.slot);
            return n !== null && n === baseSlot;
        });
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

        const nested = matchNestedMapping(layout, baseSlot, keys[0], candidateKeys);
        if (nested) {
            nested.raw = slotSource;
            return nested;
        }
    }

    return {
        keys,
        baseSlot: safeBaseSlot(baseSlot),
        raw: slotSource,
    };
}

const MAX_NESTED_CANDIDATES = 32;

/**
 * keccak256(abi.encodePacked(padded32(key), padded32(slot))) -- Solidity mapping slot.
 *
 * @param key32hex - 32-byte key without `0x` (already padded)
 * @param slot - Mapping base slot
 * @return Hex hash
 */
function keccak256MappingPreimage(key32hex: string, slot: bigint): string {
    return keccak256('0x' + key32hex + slot.toString(16).padStart(64, '0'));
}

/**
 * Pad a candidate key the way solc pads mapping keys (left-padded 32 bytes).
 *
 * @param raw - Address (`0x` + 40 hex) or hex quantity
 * @return 64 hex chars, or `null` if the value cannot be a mapping key
 */
function padMappingKey(raw: string): string | null {
    const hex = raw.startsWith('0x') || raw.startsWith('0X') ? raw.slice(2) : raw;
    if (!/^[0-9a-fA-F]+$/.test(hex) || hex.length > 64) return null;
    return hex.toLowerCase().padStart(64, '0');
}

/**
 * Match `innerBase === keccak256(outerKey . variableSlot)` for nested mappings.
 *
 * @param layout - Compiler storage layout
 * @param innerBase - Second-level mapping slot from the intercepted preimage
 * @param innerKey - Key decoded from the intercepted preimage (inner mapping)
 * @param candidateKeys - Outer-key candidates (typically tx/event addresses)
 * @return Resolved slot, or `null` if no mapping matches
 */
function matchNestedMapping(
    layout: SolidityStorageLayout,
    innerBase: bigint,
    innerKey: ParsedKey,
    candidateKeys?: string[],
): ResolvedSlot | null {
    if (!layout.types || !layout.storage || !candidateKeys?.length) return null;

    const padded: string[] = [];
    for (const raw of candidateKeys) {
        if (padded.length >= MAX_NESTED_CANDIDATES) break;
        const key = padMappingKey(raw);
        if (key) padded.push(key);
    }
    if (padded.length === 0) return null;

    for (const entry of layout.storage) {
        const typeInfo = layout.types[entry.type];
        if (!typeInfo || typeInfo.encoding !== 'mapping' || !typeInfo.value) continue;
        const valueType = layout.types[typeInfo.value];
        if (!valueType || valueType.encoding !== 'mapping') continue;

        const base = parseLayoutSlot(entry.slot);
        if (base === null) continue;

        for (const key32 of padded) {
            if (BigInt(keccak256MappingPreimage(key32, base)) !== innerBase) continue;
            const outer = resolveKeysFromType(
                [detectKeyType('0x' + key32)],
                typeInfo,
                layout,
            );
            const inner = resolveKeysFromType([innerKey], valueType, layout);
            return {
                variableName: entry.label,
                variableType: typeInfo.label,
                keys: [...outer, ...inner],
                baseSlot: safeBaseSlot(base),
                raw: '',
            };
        }
    }
    return null;
}

/**
 * Parse a runtime storage slot hex quantity. Rejects array dumps and negatives.
 *
 * @param slotHex - Slot from the simulation result
 * @return Slot number, or `null` if the value is not clean hex
 */
function parseHexSlot(slotHex: string): bigint | null {
    const raw = slotHex.trim();
    if (!raw || raw.startsWith('[') || raw.startsWith('-') || /^0x-/i.test(raw)) return null;
    if (!/^(?:0x)?[0-9a-fA-F]+$/.test(raw)) return null;
    try {
        return BigInt(raw.startsWith('0x') || raw.startsWith('0X') ? raw : '0x' + raw);
    } catch {
        return null;
    }
}

/**
 * Parse a solc storage-layout slot (decimal string or number).
 *
 * @param slot - `entry.slot` from the compiler layout
 * @return Non-negative slot, or `null` if the value is not a decimal integer
 */
function parseLayoutSlot(slot: string | number): bigint | null {
    try {
        const n = typeof slot === 'number' ? BigInt(slot) : BigInt(String(slot).trim());
        return n < 0n ? null : n;
    } catch {
        return null;
    }
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
    const slotBigInt = slotHex ? parseHexSlot(slotHex) : null;
    if (slotBigInt === null) {
        return { baseSlot: -1, raw: slotHex ?? '' };
    }

    if (!layout?.storage || !layout.types) {
        return { baseSlot: safeBaseSlot(slotBigInt), raw: slotHex };
    }

    const entry = layout.storage.find(s => {
        const n = parseLayoutSlot(s.slot);
        return n !== null && n === slotBigInt;
    });
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

        const arrayBaseHash = keccak256Uint256Slot(entry.slot);
        if (!arrayBaseHash) continue;
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
                baseSlot: safeBaseSlot(parseLayoutSlot(entry.slot) ?? 0n),
                raw: '0x' + slot.toString(16).padStart(64, '0'),
                arrayIndex: index,
                structField,
            };
        }
    }

    return null;
}

/**
 * keccak256(abi.encode(uint256 slot)). Returns null if the slot is not a
 * scalar integer (array dumps, placeholders) so callers never throw.
 *
 * @param slot - Storage layout slot number as string or number
 * @return Hex hash, or `null` if encoding fails
 */
function keccak256Uint256Slot(slot: string | number): string | null {
    try {
        const n = parseLayoutSlot(slot);
        if (n === null) return null;
        return keccak256(AbiCoder.defaultAbiCoder().encode(['uint256'], [n]));
    } catch {
        return null;
    }
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
