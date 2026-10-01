import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { collectUsedAddresses } from '../dist/addresses.js';

const SENDER = '0x1111111111111111111111111111111111111111';
const TO = '0x2222222222222222222222222222222222222222';
const OTHER = '0x3333333333333333333333333333333333333333';
const PACKED = '0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
const ZERO = '0x0000000000000000000000000000000000000000';

describe('collectUsedAddresses', () => {
    it('puts sender first, de-duplicates, and skips the access list', () => {
        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [],
            trace: [{ from: SENDER, to: TO, type: 'CALL' }],
            accessList: [{ address: OTHER, codeHash: '0x' + 'ab'.repeat(32), storageKeys: [] }],
        };
        assert.deepEqual(
            collectUsedAddresses(result, { from: SENDER, to: TO }),
            [SENDER, TO],
        );
    });

    it('keeps the zero address and collects log / call / storage addresses', () => {
        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [{
                name: 'Transfer',
                inputs: [
                    { name: 'from', type: 'address', value: ZERO },
                    { name: 'to', type: 'address', value: OTHER },
                ],
                raw: { address: TO, topics: [], data: '0x' },
            }],
            trace: [{ from: SENDER, to: TO, type: 'CALL' }],
            stateChanges: [{ address: TO, storage: [] }],
        };
        const extra = {
            decodedCall: {
                name: 'transfer',
                signature: 'transfer(address,uint256)',
                params: [{ name: 'to', type: 'address', value: OTHER }],
            },
            decodedEvents: [{
                name: 'Transfer',
                signature: 'Transfer(address,address,uint256)',
                params: [
                    { name: 'from', type: 'address', value: ZERO },
                    { name: 'to', type: 'address', value: OTHER },
                ],
            }],
            decodedTrace: [{
                name: 'transfer',
                signature: 'transfer(address,uint256)',
                params: [{ name: 'to', type: 'address', value: OTHER }],
            }],
            resolvedStorage: new Map([[TO, [{
                baseSlot: 0,
                keys: [{ type: 'address', value: PACKED }],
            }]]]),
        };
        assert.deepEqual(
            collectUsedAddresses(result, { from: SENDER, to: TO }, extra),
            [SENDER, TO, ZERO, OTHER, PACKED],
        );
    });

    it('extracts addresses from address[] text and packed storage members', () => {
        const word = '0x' + '00'.repeat(12) + PACKED.slice(2);
        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [],
            stateChanges: [{
                address: TO,
                // Same packed owner before/after: only the address member matters here.
                storage: [{ slot: '0x0', previousValue: word, newValue: word }],
            }],
        };
        const extra = {
            decodedCall: {
                name: 'batch',
                signature: 'batch(address[])',
                params: [{
                    name: 'recipients',
                    type: 'address[]',
                    value: `[${OTHER}, ${PACKED}]`,
                }],
            },
            resolvedStorage: new Map([[TO, [{
                baseSlot: 0,
                members: [{
                    variableName: 'owner',
                    variableType: 'address',
                    offset: 0,
                    numberOfBytes: 20,
                }],
            }]]]),
        };
        assert.deepEqual(
            collectUsedAddresses(result, { from: SENDER, to: TO }, extra),
            [SENDER, TO, OTHER, PACKED],
        );
    });

    it('ignores invalid addresses and empty inputs', () => {
        assert.deepEqual(
            collectUsedAddresses(
                { gasUsed: '0x1', status: '0x1', returnValue: '0x', logs: [] },
                { from: 'not-an-address', to: '0x1234' },
            ),
            [],
        );
    });
});
