import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { parseSourceMap, instructionPcs, resolvePcs } from '../dist/source_map.js';

/** JUMPDEST opcode. */
const JUMPDEST = '5b';
/** PUSH1 opcode. */
const PUSH1 = '60';

/**
 * Build a runtime bytecode string from a list of `0x..` opcode/immediate
 * chunks. Used to keep the test-side bytecode readable.
 *
 * @param {string[]} chunks - Hex fragments without a `0x` prefix
 * @return {string} Concatenated `0x` bytecode
 */
function bytecode(...chunks) {
    return '0x' + chunks.join('');
}

describe('parseSourceMap', () => {
    it('parses one entry per instruction with delta encoding', () => {
        // s:l:f, then carry, then only s changes, then only l changes, then reset f.
        const entries = parseSourceMap('10:5:0;;15:;15:20;15:20:2');
        assert.deepEqual(entries, [
            { start: 10, length: 5, file: 0 },
            { start: 10, length: 5, file: 0 },
            { start: 15, length: 5, file: 0 },
            { start: 15, length: 20, file: 0 },
            { start: 15, length: 20, file: 2 },
        ]);
    });

    it('returns an empty array for an empty map', () => {
        assert.deepEqual(parseSourceMap(''), []);
    });
});

describe('instructionPcs', () => {
    it('skips PUSH immediates when computing per-instruction byte PCs', () => {
        // JUMPDEST, PUSH1 0x42, JUMPDEST → 3 instructions at PCs 0, 1, 3.
        const pcs = instructionPcs(bytecode(JUMPDEST, PUSH1, '42', JUMPDEST));
        assert.deepEqual(pcs, [0, 1, 3]);
    });

    it('returns an empty array for malformed hex', () => {
        assert.deepEqual(instructionPcs('0xzz'), []);
        assert.deepEqual(instructionPcs('0x1'), []);
    });
});

describe('resolvePcs', () => {
    it('maps two executed JUMPDESTs to two source ranges in the same file', () => {
        // Instruction 0: JUMPDEST at PC 0    → source 10..15
        // Instruction 1: PUSH1 at PC 1       → source 30..40 (skipped by JUMPDEST-only filter)
        // Instruction 2: JUMPDEST at PC 3    → source 50..60
        const map = {
            sourceMap: '10:5:0;30:10:0;50:10:0',
            runtimeBytecode: bytecode(JUMPDEST, PUSH1, '42', JUMPDEST),
            sourceIndex: new Map([[0, 'A.sol']]),
        };
        const hits = resolvePcs(['0x0', '0x1', '0x3'], map);
        assert.deepEqual(hits, [
            { pc: 0, filename: 'A.sol', start: 10, length: 5 },
            { pc: 3, filename: 'A.sol', start: 50, length: 10 },
        ]);
    });

    it('drops PCs whose entry has no file id', () => {
        const map = {
            sourceMap: '10:5:0;20:5:-1',
            runtimeBytecode: bytecode(JUMPDEST, JUMPDEST),
            sourceIndex: new Map([[0, 'A.sol']]),
        };
        const hits = resolvePcs(['0', '1'], map);
        assert.deepEqual(hits, [{ pc: 0, filename: 'A.sol', start: 10, length: 5 }]);
    });

    it('deduplicates PCs and accepts decimal or hex input', () => {
        const map = {
            sourceMap: '10:5:0;20:5:0',
            runtimeBytecode: bytecode(JUMPDEST, JUMPDEST),
            sourceIndex: new Map([[0, 'A.sol']]),
        };
        const hits = resolvePcs(['0x0', '0'], map);
        assert.equal(hits.length, 1);
    });

    it('ignores PCs that do not point at a JUMPDEST', () => {
        // PC 0 is JUMPDEST, PC 1 is PUSH1 (not JUMPDEST).
        const map = {
            sourceMap: '10:5:0;20:5:0',
            runtimeBytecode: bytecode(JUMPDEST, PUSH1, '00'),
            sourceIndex: new Map([[0, 'A.sol']]),
        };
        const hits = resolvePcs(['0x0', '0x1'], map);
        assert.deepEqual(hits, [{ pc: 0, filename: 'A.sol', start: 10, length: 5 }]);
    });

    it('drops PCs beyond the runtime bytecode length', () => {
        const map = {
            sourceMap: '10:5:0',
            runtimeBytecode: bytecode(JUMPDEST),
            sourceIndex: new Map([[0, 'A.sol']]),
        };
        // PC 0 is fine; PC 0xffff is well past a 1-byte contract and must not
        // read into unallocated space nor pull the entry at that index.
        const hits = resolvePcs(['0x0', '0xffff'], map);
        assert.deepEqual(hits, [{ pc: 0, filename: 'A.sol', start: 10, length: 5 }]);
    });

    it('drops entries with start = -1 (compiler-internal ranges without source)', () => {
        // Second instruction has no source range — solc emits `-1:-1:-1` for
        // stubs in the metadata footer or for injected boilerplate; those
        // must not map to "offset 0 in the first file".
        const map = {
            sourceMap: '10:5:0;-1:-1:-1',
            runtimeBytecode: bytecode(JUMPDEST, JUMPDEST),
            sourceIndex: new Map([[0, 'A.sol']]),
        };
        const hits = resolvePcs(['0', '1'], map);
        assert.deepEqual(hits, [{ pc: 0, filename: 'A.sol', start: 10, length: 5 }]);
    });

    it('drops entries whose file id is missing from the source index', () => {
        // solc emitted file id 7, but the source-index only knows 0. That
        // signals a broken `buildSourceIndex` and must not fall back to file 0.
        const map = {
            sourceMap: '10:5:7',
            runtimeBytecode: bytecode(JUMPDEST),
            sourceIndex: new Map([[0, 'A.sol']]),
        };
        assert.deepEqual(resolvePcs(['0'], map), []);
    });
});
