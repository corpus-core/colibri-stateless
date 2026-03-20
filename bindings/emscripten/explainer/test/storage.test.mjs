import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { parseSlotSource, resolveStorageSlot } from '../dist/storage.js';
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
