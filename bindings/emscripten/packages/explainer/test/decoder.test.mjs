import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { Interface } from 'ethers';
import { decodeFunctionCall, decodeEventLog, decodeFunctionResult, decodeRevertData } from '../dist/decoder.js';
import { WETH_ABI } from './fixtures.mjs';

describe('decodeFunctionCall', () => {
    it('decodes deposit() with no params', () => {
        const result = decodeFunctionCall(WETH_ABI, '0xd0e30db0');
        assert.ok(result);
        assert.equal(result.name, 'deposit');
        assert.equal(result.params.length, 0);
    });

    it('decodes transfer(address,uint256)', () => {
        const data = '0xa9059cbb'
            + '0000000000000000000000001234567890abcdef1234567890abcdef12345678'
            + '0000000000000000000000000000000000000000000000000de0b6b3a7640000';
        const result = decodeFunctionCall(WETH_ABI, data);
        assert.ok(result);
        assert.equal(result.name, 'transfer');
        assert.equal(result.params.length, 2);
        assert.equal(result.params[0].name, 'dst');
        assert.equal(result.params[0].type, 'address');
        assert.ok(result.params[0].value.toLowerCase().includes('1234567890abcdef'));
        assert.equal(result.params[1].name, 'wad');
        assert.equal(result.params[1].type, 'uint256');
    });

    it('returns null for unknown selector', () => {
        const result = decodeFunctionCall(WETH_ABI, '0xdeadbeef');
        assert.equal(result, null);
    });

    it('returns null for empty/short data', () => {
        assert.equal(decodeFunctionCall(WETH_ABI, ''), null);
        assert.equal(decodeFunctionCall(WETH_ABI, '0x1234'), null);
    });
});

describe('decodeEventLog', () => {
    it('decodes a Deposit event', () => {
        const result = decodeEventLog(WETH_ABI, {
            topics: [
                '0xe1fffcc4923d04b559f4d29a8bfc6cda04eb5b0d3c460751c2402c5c5cc9109c',
                '0x0000000000000000000000003610bad33aac567d2c5fb03e47eec5c2172fd42a',
            ],
            data: '0x000000000000000000000000000000000000000000000000016345785d8a0000',
        });
        assert.ok(result);
        assert.equal(result.name, 'Deposit');
        assert.equal(result.params.length, 2);
        assert.equal(result.params[0].name, 'dst');
        assert.equal(result.params[0].indexed, true);
        assert.equal(result.params[1].name, 'wad');
        assert.equal(result.params[1].indexed, false);
    });

    it('decodes a Transfer event', () => {
        const result = decodeEventLog(WETH_ABI, {
            topics: [
                '0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef',
                '0x0000000000000000000000003610bad33aac567d2c5fb03e47eec5c2172fd42a',
                '0x000000000000000000000000c02aaa39b223fe8d0a0e5c4f27ead9083c756cc2',
            ],
            data: '0x000000000000000000000000000000000000000000000000016345785d8a0000',
        });
        assert.ok(result);
        assert.equal(result.name, 'Transfer');
        assert.equal(result.params.length, 3);
    });

    it('returns null for unknown topic', () => {
        const result = decodeEventLog(WETH_ABI, {
            topics: ['0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef'],
            data: '0x',
        });
        assert.equal(result, null);
    });

    it('returns null for empty topics', () => {
        assert.equal(decodeEventLog(WETH_ABI, { topics: [], data: '0x' }), null);
    });
});

describe('decodeFunctionResult', () => {
    it('decodes balanceOf return value', () => {
        const calldata = '0x70a08231'
            + '0000000000000000000000003610bad33aac567d2c5fb03e47eec5c2172fd42a';
        const resultData = '0x000000000000000000000000000000000000000000000000016345785d8a0000';
        const result = decodeFunctionResult(WETH_ABI, calldata, resultData);
        assert.ok(result);
        assert.equal(result.name, 'balanceOf');
        assert.equal(result.params.length, 1);
    });

    it('returns null for empty result data', () => {
        assert.equal(decodeFunctionResult(WETH_ABI, '0xd0e30db0', '0x'), null);
    });

    it('returns null for functions with no outputs', () => {
        const result = decodeFunctionResult(WETH_ABI, '0xd0e30db0', '0x0000000000000000000000000000000000000000000000000000000000000001');
        assert.equal(result, null);
    });
});

describe('decodeRevertData', () => {
    it('decodes Error(string) revert reason', () => {
        // Error("Insufficient balance") encoded
        const data = '0x08c379a0'
            + '0000000000000000000000000000000000000000000000000000000000000020'
            + '0000000000000000000000000000000000000000000000000000000000000014'
            + '496e73756666696369656e742062616c616e6365000000000000000000000000';
        const result = decodeRevertData(data);
        assert.ok(result);
        assert.equal(result.name, 'Error');
        assert.equal(result.signature, 'Error(string)');
        assert.equal(result.reason, 'Insufficient balance');
        assert.equal(result.params[0].name, 'reason');
        assert.equal(result.params[0].value, 'Insufficient balance');
    });

    it('decodes Panic(uint256) assert failure', () => {
        // Panic(0x01) = assert condition failed
        const data = '0x4e487b71'
            + '0000000000000000000000000000000000000000000000000000000000000001';
        const result = decodeRevertData(data);
        assert.ok(result);
        assert.equal(result.name, 'Panic');
        assert.equal(result.signature, 'Panic(uint256)');
        assert.equal(result.reason, 'Assert condition failed');
        assert.equal(result.params[0].name, 'code');
        assert.equal(result.params[0].value, '0x1');
    });

    it('decodes Panic arithmetic overflow', () => {
        const data = '0x4e487b71'
            + '0000000000000000000000000000000000000000000000000000000000000011';
        const result = decodeRevertData(data);
        assert.ok(result);
        assert.equal(result.reason, 'Arithmetic overflow or underflow');
    });

    it('decodes custom error from ABI', () => {
        const abi = [
            { name: 'InsufficientBalance', type: 'error', inputs: [
                { name: 'available', type: 'uint256' },
                { name: 'required', type: 'uint256' },
            ]},
        ];
        const iface = new Interface(abi);
        const encoded = iface.encodeErrorResult('InsufficientBalance', [100, 200]);

        const result = decodeRevertData(encoded, abi);
        assert.ok(result);
        assert.equal(result.name, 'InsufficientBalance');
        assert.equal(result.params.length, 2);
        assert.equal(result.params[0].name, 'available');
        assert.equal(result.params[1].name, 'required');
    });

    it('returns null for empty or short data', () => {
        assert.equal(decodeRevertData('0x'), null);
        assert.equal(decodeRevertData(''), null);
        assert.equal(decodeRevertData('0x1234'), null);
    });

    it('returns null for unknown selector without ABI', () => {
        const data = '0xdeadbeef' + '0000000000000000000000000000000000000000000000000000000000000001';
        assert.equal(decodeRevertData(data), null);
    });
});
