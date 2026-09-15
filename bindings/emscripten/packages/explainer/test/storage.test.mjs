import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { keccak256, AbiCoder } from 'ethers';
import { parseSlotSource, resolveStorageSlot, resolveDirectSlot } from '../dist/storage.js';
import { UNI_STORAGE_LAYOUT } from './fixtures.mjs';

const WETH_SLOT_SOURCE =
    '0x0000000000000000000000003610bad33aac567d2c5fb03e47eec5c2172fd42a' +
    '0000000000000000000000000000000000000000000000000000000000000003';

describe('parseSlotSource', () => {
    it('extracts baseSlot and keyData from a mapping preimage', () => {
        const { baseSlot, keyData } = parseSlotSource(WETH_SLOT_SOURCE);
        assert.equal(baseSlot, 3n);
        assert.equal(keyData, '0x0000000000000000000000003610bad33aac567d2c5fb03e47eec5c2172fd42a');
    });

    it('handles slot 0', () => {
        const slotSource =
            '0x0000000000000000000000001234567890abcdef1234567890abcdef12345678' +
            '0000000000000000000000000000000000000000000000000000000000000000';
        const { baseSlot, keyData } = parseSlotSource(slotSource);
        assert.equal(baseSlot, 0n);
        assert.ok(keyData.includes('1234567890abcdef'));
    });

    it('returns -1 for invalid length', () => {
        const { baseSlot } = parseSlotSource('0xdeadbeef');
        assert.equal(baseSlot, -1n);
    });

    it('handles input without 0x prefix', () => {
        const raw = WETH_SLOT_SOURCE.slice(2);
        const { baseSlot } = parseSlotSource(raw);
        assert.equal(baseSlot, 3n);
    });
});

describe('resolveStorageSlot', () => {
    it('resolves with storageLayout (UNI balances at slot 4)', () => {
        const slotSource =
            '0x0000000000000000000000003610bad33aac567d2c5fb03e47eec5c2172fd42a' +
            '0000000000000000000000000000000000000000000000000000000000000004';
        const result = resolveStorageSlot(slotSource, UNI_STORAGE_LAYOUT);
        assert.equal(result.variableName, 'balances');
        assert.equal(result.variableType, 'mapping(address => uint96)');
        assert.equal(result.baseSlot, 4);
        assert.ok(result.keys);
        assert.equal(result.keys.length, 1);
        assert.equal(result.keys[0].type, 'address');
        assert.ok(result.keys[0].value.includes('3610bad33aac567d2c5fb03e47eec5c2172fd42a'));
    });

    it('resolves without storageLayout (heuristic key detection)', () => {
        const result = resolveStorageSlot(WETH_SLOT_SOURCE, null);
        assert.equal(result.variableName, undefined);
        assert.equal(result.baseSlot, 3);
        assert.ok(result.keys);
        assert.equal(result.keys[0].type, 'address');
        assert.ok(result.keys[0].value.includes('3610bad33aac567d2c5fb03e47eec5c2172fd42a'));
    });

    it('handles invalid slotSource', () => {
        const result = resolveStorageSlot('0xshort', null);
        assert.equal(result.baseSlot, -1);
    });

    it('resolves nested mapping (allowances at slot 3)', () => {
        const slotSource =
            '0x0000000000000000000000005678abcdef1234567890abcdef1234567890abcd' +
            '0000000000000000000000000000000000000000000000000000000000000003';
        const result = resolveStorageSlot(slotSource, UNI_STORAGE_LAYOUT);
        assert.equal(result.variableName, 'allowances');
        assert.equal(result.baseSlot, 3);
    });

    it('resolves a nested mapping from the inner keccak preimage plus outer-key candidates', () => {
        const owner = '0xedf8a8bf77e25b8a0ebe4a26889fa12f0d5485d5';
        const spender = '0x40aa958dd87fc8305b97f2ba922cddca374bcd7f';
        const inner = keccak256(AbiCoder.defaultAbiCoder().encode(['address', 'uint256'], [owner, 3n]));
        const slotSource = '0x' + spender.slice(2).padStart(64, '0') + inner.slice(2);
        const result = resolveStorageSlot(slotSource, UNI_STORAGE_LAYOUT, [owner, spender]);
        assert.equal(result.variableName, 'allowances');
        assert.equal(result.baseSlot, 3);
        assert.equal(result.keys?.length, 2);
        assert.equal(result.keys[0].type, 'address');
        assert.ok(result.keys[0].value.toLowerCase().includes(owner.slice(2)));
        assert.ok(result.keys[1].value.toLowerCase().includes(spender.slice(2)));
    });

    it('does not invent a nested mapping name without outer-key candidates', () => {
        const owner = '0xedf8a8bf77e25b8a0ebe4a26889fa12f0d5485d5';
        const spender = '0x40aa958dd87fc8305b97f2ba922cddca374bcd7f';
        const inner = keccak256(AbiCoder.defaultAbiCoder().encode(['address', 'uint256'], [owner, 3n]));
        const slotSource = '0x' + spender.slice(2).padStart(64, '0') + inner.slice(2);
        const result = resolveStorageSlot(slotSource, UNI_STORAGE_LAYOUT);
        assert.equal(result.variableName, undefined);
    });

    it('does not match a nested mapping when outer-key candidates are unrelated', () => {
        const owner = '0xedf8a8bf77e25b8a0ebe4a26889fa12f0d5485d5';
        const spender = '0x40aa958dd87fc8305b97f2ba922cddca374bcd7f';
        const inner = keccak256(AbiCoder.defaultAbiCoder().encode(['address', 'uint256'], [owner, 3n]));
        const slotSource = '0x' + spender.slice(2).padStart(64, '0') + inner.slice(2);
        const result = resolveStorageSlot(slotSource, UNI_STORAGE_LAYOUT, [
            '0x1111111111111111111111111111111111111111',
            'not-a-key',
            '0x' + 'aa'.repeat(33),
        ]);
        assert.equal(result.variableName, undefined);
    });

    it('skips invalid candidates and still matches a valid outer key', () => {
        const owner = '0xedf8a8bf77e25b8a0ebe4a26889fa12f0d5485d5';
        const spender = '0x40aa958dd87fc8305b97f2ba922cddca374bcd7f';
        const inner = keccak256(AbiCoder.defaultAbiCoder().encode(['address', 'uint256'], [owner, 3n]));
        const slotSource = '0x' + spender.slice(2).padStart(64, '0') + inner.slice(2);
        const result = resolveStorageSlot(slotSource, UNI_STORAGE_LAYOUT, [
            '',
            'not-a-key',
            '0X' + owner.slice(2).toUpperCase(),
            '0x' + 'aa'.repeat(33),
        ]);
        assert.equal(result.variableName, 'allowances');
        assert.equal(result.keys?.length, 2);
        assert.ok(result.keys[0].value.toLowerCase().includes(owner.slice(2)));
        assert.ok(result.keys[1].value.toLowerCase().includes(spender.slice(2)));
    });

    it('picks the nested mapping whose keccak(outer . slot) matches', () => {
        const owner = '0xedf8a8bf77e25b8a0ebe4a26889fa12f0d5485d5';
        const spender = '0x40aa958dd87fc8305b97f2ba922cddca374bcd7f';
        const layout = {
            storage: [
                { slot: '3', type: 't_mapping(t_address,t_mapping(t_address,t_uint256))', astId: 1, label: 'allowances', offset: 0, contract: 'C.sol:C' },
                { slot: '5', type: 't_mapping(t_address,t_mapping(t_address,t_uint256))', astId: 2, label: 'freeze', offset: 0, contract: 'C.sol:C' },
            ],
            types: {
                t_address: { label: 'address', encoding: 'inplace', numberOfBytes: '20' },
                t_uint256: { label: 'uint256', encoding: 'inplace', numberOfBytes: '32' },
                't_mapping(t_address,t_uint256)': {
                    key: 't_address', label: 'mapping(address => uint256)', value: 't_uint256', encoding: 'mapping', numberOfBytes: '32',
                },
                't_mapping(t_address,t_mapping(t_address,t_uint256))': {
                    key: 't_address', label: 'mapping(address => mapping(address => uint256))',
                    value: 't_mapping(t_address,t_uint256)', encoding: 'mapping', numberOfBytes: '32',
                },
            },
        };
        const inner = keccak256(AbiCoder.defaultAbiCoder().encode(['address', 'uint256'], [owner, 5n]));
        const slotSource = '0x' + spender.slice(2).padStart(64, '0') + inner.slice(2);
        const result = resolveStorageSlot(slotSource, layout, [owner, spender]);
        assert.equal(result.variableName, 'freeze');
        assert.equal(result.baseSlot, 5);
        assert.equal(result.keys?.length, 2);
        assert.ok(result.keys[0].value.toLowerCase().includes(owner.slice(2)));
    });

    it('resolves a nested mapping whose outer key is a uint256', () => {
        const outerKey = 7n;
        const spender = '0x40aa958dd87fc8305b97f2ba922cddca374bcd7f';
        const layout = {
            storage: [
                { slot: '2', type: 't_mapping(t_uint256,t_mapping(t_address,t_uint256))', astId: 1, label: 'claims', offset: 0, contract: 'C.sol:C' },
            ],
            types: {
                t_address: { label: 'address', encoding: 'inplace', numberOfBytes: '20' },
                t_uint256: { label: 'uint256', encoding: 'inplace', numberOfBytes: '32' },
                't_mapping(t_address,t_uint256)': {
                    key: 't_address', label: 'mapping(address => uint256)', value: 't_uint256', encoding: 'mapping', numberOfBytes: '32',
                },
                't_mapping(t_uint256,t_mapping(t_address,t_uint256))': {
                    key: 't_uint256', label: 'mapping(uint256 => mapping(address => uint256))',
                    value: 't_mapping(t_address,t_uint256)', encoding: 'mapping', numberOfBytes: '32',
                },
            },
        };
        const inner = keccak256(AbiCoder.defaultAbiCoder().encode(['uint256', 'uint256'], [outerKey, 2n]));
        const slotSource = '0x' + spender.slice(2).padStart(64, '0') + inner.slice(2);
        const result = resolveStorageSlot(slotSource, layout, ['0x7']);
        assert.equal(result.variableName, 'claims');
        assert.equal(result.baseSlot, 2);
        assert.equal(result.keys?.length, 2);
        assert.equal(result.keys[0].type, 'uint256');
        assert.equal(result.keys[0].value, '7');
        assert.ok(result.keys[1].value.toLowerCase().includes(spender.slice(2)));
    });

    it('detects uint256 keys when leading bytes are non-zero', () => {
        const slotSource =
            '0x0000000000000000000000000000000000000000000000000000000000000042' +
            '0000000000000000000000000000000000000000000000000000000000000004';
        const result = resolveStorageSlot(slotSource, null);
        assert.ok(result.keys);
        assert.equal(result.keys[0].type, 'uint256');
        assert.equal(result.keys[0].value, '66');
    });
});

describe('resolveDirectSlot', () => {
    it('resolves a direct variable by slot number', () => {
        const result = resolveDirectSlot('0x0000000000000000000000000000000000000000000000000000000000000000', UNI_STORAGE_LAYOUT);
        assert.equal(result.variableName, 'totalSupply');
        assert.equal(result.variableType, 'uint256');
        assert.equal(result.baseSlot, 0);
    });

    it('resolves minter at slot 1', () => {
        const result = resolveDirectSlot('0x0000000000000000000000000000000000000000000000000000000000000001', UNI_STORAGE_LAYOUT);
        assert.equal(result.variableName, 'minter');
        assert.equal(result.variableType, 'address');
    });

    it('resolves mintingAllowedAfter at slot 2', () => {
        const result = resolveDirectSlot('0x0000000000000000000000000000000000000000000000000000000000000002', UNI_STORAGE_LAYOUT);
        assert.equal(result.variableName, 'mintingAllowedAfter');
    });

    it('returns unresolved for unknown slot', () => {
        const result = resolveDirectSlot('0x0000000000000000000000000000000000000000000000000000000000000099', UNI_STORAGE_LAYOUT);
        assert.equal(result.variableName, undefined);
        assert.equal(result.baseSlot, 0x99);
    });

    it('returns unresolved without layout', () => {
        const result = resolveDirectSlot('0x0000000000000000000000000000000000000000000000000000000000000000', null);
        assert.equal(result.variableName, undefined);
        assert.equal(result.baseSlot, 0);
    });

    it('does not throw on negative hex or array dumps', () => {
        const neg = resolveDirectSlot('0x-31380', UNI_STORAGE_LAYOUT);
        assert.equal(neg.variableName, undefined);
        assert.equal(neg.baseSlot, -1);
        const arr = resolveDirectSlot('[0x309066af5db7ee2d246, 0x0]', UNI_STORAGE_LAYOUT);
        assert.equal(arr.baseSlot, -1);
        assert.equal(arr.raw, '[0x309066af5db7ee2d246, 0x0]');
        const empty = resolveDirectSlot('', UNI_STORAGE_LAYOUT);
        assert.equal(empty.baseSlot, -1);
        assert.equal(empty.raw, '');
        const decimalNeg = resolveDirectSlot('-31380', UNI_STORAGE_LAYOUT);
        assert.equal(decimalNeg.baseSlot, -1);
        const garbage = resolveDirectSlot('not-hex', UNI_STORAGE_LAYOUT);
        assert.equal(garbage.baseSlot, -1);
        assert.equal(garbage.variableName, undefined);
    });
});

const ARRAY_LAYOUT = {
    storage: [
        { slot: '5', type: 't_array(t_uint256)dyn_storage', astId: 1, label: 'items', offset: 0, contract: 'C.sol:C' },
        { slot: '-1', type: 't_array(t_uint256)dyn_storage', astId: 3, label: 'brokenNeg', offset: 0, contract: 'C.sol:C' },
    ],
    types: {
        't_array(t_uint256)dyn_storage': {
            label: 'uint256[]',
            encoding: 'dynamic_array',
            numberOfBytes: '32',
            base: 't_uint256',
        },
        't_uint256': { label: 'uint256', encoding: 'inplace', numberOfBytes: '32' },
    },
};

const STRUCT_ARRAY_LAYOUT = {
    storage: [
        { slot: '5', type: 't_array(t_struct(Item)storage)dyn_storage', astId: 1, label: 'items', offset: 0, contract: 'C.sol:C' },
    ],
    types: {
        't_array(t_struct(Item)storage)dyn_storage': {
            label: 'struct Item[]',
            encoding: 'dynamic_array',
            numberOfBytes: '32',
            base: 't_struct(Item)storage',
        },
        't_struct(Item)storage': {
            label: 'struct Item',
            encoding: 'inplace',
            numberOfBytes: '64',
            members: [
                { slot: '0', type: 't_uint256', astId: 2, label: 'a', offset: 0, contract: 'C.sol:C' },
                { slot: '1', type: 't_uint256', astId: 3, label: 'b', offset: 0, contract: 'C.sol:C' },
            ],
        },
        't_uint256': { label: 'uint256', encoding: 'inplace', numberOfBytes: '32' },
    },
};

function arrayElementSlot(baseSlot, index, elementSlots = 1n) {
    const arrayStart = BigInt(keccak256(AbiCoder.defaultAbiCoder().encode(['uint256'], [BigInt(baseSlot)])));
    const slot = arrayStart + BigInt(index) * elementSlots;
    return '0x' + slot.toString(16).padStart(64, '0');
}

describe('resolveDirectSlot dynamic arrays', () => {
    it('resolves an element of a dynamic uint256 array', () => {
        const result = resolveDirectSlot(arrayElementSlot(5, 2), ARRAY_LAYOUT);
        assert.equal(result.variableName, 'items');
        assert.equal(result.variableType, 'uint256[]');
        assert.equal(result.arrayIndex, 2);
        assert.equal(result.baseSlot, 5);
    });

    it('resolves a struct field inside a dynamic array element', () => {
        const arrayStart = BigInt(keccak256(AbiCoder.defaultAbiCoder().encode(['uint256'], [5n])));
        const slot = arrayStart + 1n * 2n + 1n; // items[1].b
        const resolved = resolveDirectSlot('0x' + slot.toString(16).padStart(64, '0'), STRUCT_ARRAY_LAYOUT);
        assert.equal(resolved.variableName, 'items');
        assert.equal(resolved.arrayIndex, 1);
        assert.equal(resolved.structField, 'b');
    });

    it('does not throw when a layout array slot is negative', () => {
        const slot = arrayElementSlot(5, 0);
        let result;
        assert.doesNotThrow(() => {
            result = resolveDirectSlot(slot, ARRAY_LAYOUT);
        });
        assert.equal(result.variableName, 'items');
        assert.equal(result.arrayIndex, 0);
        assert.notEqual(result.variableName, 'brokenNeg');
    });
});
