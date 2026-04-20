import { describe, it, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import { enrichSimulation, toEnhancedResult } from '../dist/enrich.js';
import { WETH_DEPOSIT_RESULT, TX_PARAMS, WETH_ABI, REVERTED_TX_RESULT } from './fixtures.mjs';

describe('enrichSimulation', () => {
    let originalFetch;

    beforeEach(() => {
        originalFetch = globalThis.fetch;
    });

    afterEach(() => {
        globalThis.fetch = originalFetch;
    });

    function mockSourcify(abiByAddress = {}) {
        globalThis.fetch = async (url) => {
            const match = url.match(/\/v2\/contract\/\d+\/([^?]+)/);
            const addr = match?.[1]?.toLowerCase();
            const abi = abiByAddress[addr] || null;
            return new Response(JSON.stringify({
                abi,
                sources: abi ? { 'Contract.sol': { content: 'pragma solidity ^0.8.0;' } } : null,
                compilation: abi ? { compilerVersion: '0.8.0', name: 'Contract' } : null,
                stdJsonInput: null,
            }), { status: 200, headers: { 'Content-Type': 'application/json' } });
        };
    }

    it('decodes the main function call via ABI', async () => {
        mockSourcify({
            '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2': WETH_ABI,
        });

        const ctx = await enrichSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, 1);
        assert.ok(ctx.decodedCall);
        assert.equal(ctx.decodedCall.name, 'deposit');
    });

    it('decodes trace entries', async () => {
        mockSourcify({
            '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2': WETH_ABI,
        });

        const ctx = await enrichSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, 1);
        assert.ok(ctx.decodedTrace.length > 0);
        assert.equal(ctx.decodedTrace[0]?.name, 'deposit');
    });

    it('resolves storage slots via slotSource', async () => {
        mockSourcify({
            '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2': WETH_ABI,
        });

        const ctx = await enrichSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, 1);
        const wethSlots = ctx.resolvedStorage.get('0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2');
        assert.ok(wethSlots);
        assert.equal(wethSlots.length, 1);
        assert.equal(wethSlots[0].baseSlot, 3);
        assert.ok(wethSlots[0].keys);
        assert.equal(wethSlots[0].keys[0].type, 'address');
    });

    it('fetches metadata for all involved contracts', async () => {
        const fetchedAddresses = new Set();
        globalThis.fetch = async (url) => {
            const match = url.match(/\/v2\/contract\/\d+\/([^?]+)/);
            if (match) fetchedAddresses.add(match[1].toLowerCase());
            return new Response(JSON.stringify({
                abi: null, sources: null, compilation: null, stdJsonInput: null,
            }), { status: 200, headers: { 'Content-Type': 'application/json' } });
        };

        await enrichSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, 1);
        assert.ok(fetchedAddresses.has('0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2'));
        assert.ok(fetchedAddresses.has('0x3610bad33aac567d2c5fb03e47eec5c2172fd42a'));
    });

    it('handles Sourcify failures gracefully', async () => {
        globalThis.fetch = async () => { throw new Error('ECONNREFUSED'); };

        const ctx = await enrichSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, 1);
        assert.equal(ctx.decodedCall, undefined);
        assert.ok(ctx.contracts.size > 0);
    });

    it('skips already-decoded events', async () => {
        mockSourcify({
            '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2': WETH_ABI,
        });

        const ctx = await enrichSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, 1);
        assert.ok(ctx.decodedEvents.every(e => e === null));
    });

    it('decodes Error(string) revert reason on failed tx', async () => {
        mockSourcify({});

        const ctx = await enrichSimulation(REVERTED_TX_RESULT, TX_PARAMS, 1);
        assert.ok(ctx.decodedError);
        assert.equal(ctx.decodedError.name, 'Error');
        assert.equal(ctx.decodedError.reason, 'Insufficient balance');
    });

    it('does not decode error on successful tx', async () => {
        mockSourcify({});

        const ctx = await enrichSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, 1);
        assert.equal(ctx.decodedError, undefined);
    });
});

describe('toEnhancedResult', () => {
    it('merges enriched context into the simulation result', async () => {
        const originalFetch = globalThis.fetch;
        globalThis.fetch = async (url) => {
            const match = url.match(/\/v2\/contract\/\d+\/([^?]+)/);
            const addr = match?.[1]?.toLowerCase();
            const abi = addr === '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2' ? WETH_ABI : null;
            return new Response(JSON.stringify({
                abi, sources: null, compilation: null, stdJsonInput: null,
            }), { status: 200, headers: { 'Content-Type': 'application/json' } });
        };

        try {
            const ctx = await enrichSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, 1);
            const enhanced = toEnhancedResult(WETH_DEPOSIT_RESULT, ctx, 'Test explanation');

            assert.equal(enhanced.explanation, 'Test explanation');
            assert.equal(enhanced.gasUsed, WETH_DEPOSIT_RESULT.gasUsed);
            assert.equal(enhanced.status, WETH_DEPOSIT_RESULT.status);
            assert.equal(enhanced.returnValue, WETH_DEPOSIT_RESULT.returnValue);

            assert.ok(enhanced.decodedCall);
            assert.equal(enhanced.decodedCall.name, 'deposit');

            assert.equal(enhanced.trace.length, 1);
            assert.ok(enhanced.trace[0].decoded);
            assert.equal(enhanced.trace[0].decoded.name, 'deposit');
            assert.equal(enhanced.trace[0].from, WETH_DEPOSIT_RESULT.trace[0].from);

            assert.equal(enhanced.stateChanges.length, 1);
            assert.equal(enhanced.stateChanges[0].address, WETH_DEPOSIT_RESULT.stateChanges[0].address);
            assert.ok(enhanced.stateChanges[0].storage[0].resolved);
            assert.equal(enhanced.stateChanges[0].storage[0].resolved.baseSlot, 3);
            assert.ok(enhanced.stateChanges[0].balance);

            assert.equal(enhanced.logs.length, 2);

            const json = JSON.stringify(enhanced);
            assert.ok(json, 'result must be JSON-serializable');
        } finally {
            globalThis.fetch = originalFetch;
        }
    });

    it('preserves original data when no enrichment is available', () => {
        const emptyContext = {
            contracts: new Map(),
            resolvedStorage: new Map(),
            decodedTrace: [],
            decodedEvents: [],
        };

        const enhanced = toEnhancedResult(WETH_DEPOSIT_RESULT, emptyContext, 'No enrichment');

        assert.equal(enhanced.explanation, 'No enrichment');
        assert.equal(enhanced.decodedCall, undefined);
        assert.equal(enhanced.trace[0].decoded, undefined);
        assert.equal(enhanced.stateChanges[0].storage[0].resolved, undefined);
        assert.equal(enhanced.logs[0].decoded, undefined);
    });

    it('includes decoded error in enhanced result for reverted tx', () => {
        const ctx = {
            contracts: new Map(),
            resolvedStorage: new Map(),
            decodedTrace: [],
            decodedEvents: [],
            decodedError: {
                name: 'Error',
                signature: 'Error(string)',
                params: [{ name: 'reason', type: 'string', value: 'Insufficient balance' }],
                reason: 'Insufficient balance',
            },
        };

        const enhanced = toEnhancedResult(REVERTED_TX_RESULT, ctx, 'Tx failed');

        assert.equal(enhanced.error.name, 'Error');
        assert.equal(enhanced.error.reason, 'Insufficient balance');
        assert.equal(enhanced.status, '0x0');
    });
});
