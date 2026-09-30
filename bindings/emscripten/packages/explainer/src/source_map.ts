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

import type { ContractSourceMap } from './types.js';

/** JUMPDEST opcode. Only PCs pointing at a `0x5b` are mapped. */
const OP_JUMPDEST = 0x5b;

/** First `PUSH1` (`0x60`) opcode. `PUSH0` (`0x5f`) has no immediate. */
const OP_PUSH1 = 0x60;
/** Last `PUSH32` (`0x7f`) opcode. */
const OP_PUSH32 = 0x7f;

/** Solidity source-map entry: `s:l:f:j:m`, all optional (delta encoded). */
export interface SourceMapEntry {
    /** Byte offset in source `f`. `-1` when the compiler emitted no location. */
    start: number;
    /** Byte length of the source range. `-1` when unavailable. */
    length: number;
    /** Source id (matches `output.sources[...].id`), or `-1` for compiler-internal. */
    file: number;
}

/** Executed JUMPDEST resolved to a source location. */
export interface SourceHit {
    /** Program counter of the JUMPDEST in the deployed bytecode. */
    pc: number;
    /** Solidity source filename. */
    filename: string;
    /** Byte offset into the source file. */
    start: number;
    /** Byte length of the source range. */
    length: number;
}

/**
 * Parse a Solidity `evm.deployedBytecode.sourceMap` string.
 *
 * The format is one delta-encoded entry per instruction, separated by `;`.
 * Each entry has up to five colon-separated fields (`s:l:f:j:m`). An empty
 * field carries over the previous value. Only `s`, `l`, `f` are exposed
 * because JUMPDEST resolution does not need `j` or `m`.
 *
 * @param sourceMap - Raw source-map string, may be empty
 * @return One `SourceMapEntry` per instruction, in program order
 */
export function parseSourceMap(sourceMap: string): SourceMapEntry[] {
    if (!sourceMap) return [];
    const chunks = sourceMap.split(';');
    const out: SourceMapEntry[] = new Array(chunks.length);
    let start = -1;
    let length = -1;
    let file = -1;
    for (let i = 0; i < chunks.length; i++) {
        const chunk = chunks[i];
        if (chunk) {
            const parts = chunk.split(':');
            if (parts[0]) {
                const n = Number.parseInt(parts[0], 10);
                if (Number.isFinite(n)) start = n;
            }
            if (parts.length > 1 && parts[1]) {
                const n = Number.parseInt(parts[1], 10);
                if (Number.isFinite(n)) length = n;
            }
            if (parts.length > 2 && parts[2]) {
                const n = Number.parseInt(parts[2], 10);
                if (Number.isFinite(n)) file = n;
            }
        }
        out[i] = { start, length, file };
    }
    return out;
}

/**
 * Walk the runtime bytecode and, for every instruction, record its byte PC.
 *
 * Solidity source-map indexing is per instruction (source-map index N ↔ the
 * N-th opcode), while the C-core reports byte PCs. `PUSH1..PUSH32` opcodes are
 * followed by 1..32 immediate bytes that must be skipped when converting. The
 * returned array has one PC per source-map entry.
 *
 * @param runtimeBytecode - `0x...` runtime bytecode
 * @return Byte PC of each instruction, in the same order as the source-map
 */
export function instructionPcs(runtimeBytecode: string): number[] {
    const hex = runtimeBytecode.startsWith('0x') || runtimeBytecode.startsWith('0X')
        ? runtimeBytecode.slice(2)
        : runtimeBytecode;
    if (!hex.length || hex.length % 2 !== 0) return [];
    const pcs: number[] = [];
    for (let i = 0; i < hex.length;) {
        const pc = i / 2;
        const op = Number.parseInt(hex.slice(i, i + 2), 16);
        if (!Number.isFinite(op)) return [];
        pcs.push(pc);
        i += 2;
        if (op >= OP_PUSH1 && op <= OP_PUSH32) {
            const skip = (op - OP_PUSH1 + 1) * 2;
            i += skip;
        }
    }
    return pcs;
}

/**
 * Map a set of executed JUMPDEST PCs to source-file positions.
 *
 * Non-JUMPDEST PCs and PCs without a source-map entry are dropped. Callers
 * receive one `SourceHit` per successfully mapped PC; a PC that maps to a
 * different opcode (e.g. the compiler moved code) is skipped rather than
 * mis-attributed.
 *
 * @param pcs - PCs reported by the prover (any format understood by `parseInt`)
 * @param map - Runtime source-map + bytecode + source id table
 * @return Source hits (may be empty)
 */
export function resolvePcs(pcs: string[], map: ContractSourceMap): SourceHit[] {
    const entries = parseSourceMap(map.sourceMap);
    if (!entries.length) return [];
    const insnPcs = instructionPcs(map.runtimeBytecode);
    if (!insnPcs.length) return [];

    const pcToIndex = new Map<number, number>();
    for (let i = 0; i < insnPcs.length; i++) pcToIndex.set(insnPcs[i], i);

    const hex = map.runtimeBytecode.startsWith('0x') || map.runtimeBytecode.startsWith('0X')
        ? map.runtimeBytecode.slice(2)
        : map.runtimeBytecode;
    const out: SourceHit[] = [];
    const seen = new Set<number>();
    for (const raw of pcs) {
        const pc = parsePc(raw);
        if (pc < 0 || seen.has(pc)) continue;
        seen.add(pc);
        const bytePos = pc * 2;
        if (bytePos + 2 > hex.length) continue;
        const op = Number.parseInt(hex.slice(bytePos, bytePos + 2), 16);
        if (op !== OP_JUMPDEST) continue;
        const idx = pcToIndex.get(pc);
        if (idx === undefined || idx >= entries.length) continue;
        const entry = entries[idx];
        if (entry.file < 0 || entry.start < 0 || entry.length <= 0) continue;
        const filename = map.sourceIndex.get(entry.file);
        if (!filename) continue;
        out.push({ pc, filename, start: entry.start, length: entry.length });
    }
    return out;
}

/**
 * Parse a PC value that may arrive as decimal or `0x...`.
 *
 * @param raw - PC from `SimulationResult.positions[].pcs`
 * @return Non-negative PC, or `-1` when the input cannot be parsed
 */
function parsePc(raw: string): number {
    if (!raw) return -1;
    const trimmed = raw.trim();
    if (!trimmed) return -1;
    const n = trimmed.startsWith('0x') || trimmed.startsWith('0X')
        ? Number.parseInt(trimmed.slice(2), 16)
        : Number.parseInt(trimmed, 10);
    return Number.isFinite(n) && n >= 0 ? n : -1;
}
