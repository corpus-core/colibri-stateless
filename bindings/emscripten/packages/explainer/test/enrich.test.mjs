import { describe, it, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import { enrichSimulation, toEnhancedResult } from '../dist/enrich.js';
import { resetSourcifyStateForTests, setSourcifyClockForTests } from '../dist/sourcify.js';
import { WETH_DEPOSIT_RESULT, TX_PARAMS, WETH_ABI, REVERTED_TX_RESULT } from './fixtures.mjs';

function memoryCache() {
    const store = new Map();
    return {
        store,
        get: async (key) => store.get(key) ?? null,
        set: async (key, value) => { store.set(key, value); },
    };
}

describe('enrichSimulation', () => {
    let originalFetch;

    beforeEach(() => {
        originalFetch = globalThis.fetch;
        resetSourcifyStateForTests();
        setSourcifyClockForTests(() => Date.now(), async () => { });
    });

    afterEach(() => {
        globalThis.fetch = originalFetch;
        resetSourcifyStateForTests();
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

        const ctx = await enrichSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, 1, { cache: memoryCache() });
        assert.ok(ctx.decodedCall);
        assert.equal(ctx.decodedCall.name, 'deposit');
    });

    it('decodes trace entries', async () => {
        mockSourcify({
            '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2': WETH_ABI,
        });

        const ctx = await enrichSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, 1, { cache: memoryCache() });
        assert.ok(ctx.decodedTrace.length > 0);
        assert.equal(ctx.decodedTrace[0]?.name, 'deposit');
    });

    it('resolves storage slots via slotSource', async () => {
        mockSourcify({
            '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2': WETH_ABI,
        });

        const ctx = await enrichSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, 1, { cache: memoryCache() });
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

        await enrichSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, 1, { cache: memoryCache() });
        assert.ok(fetchedAddresses.has('0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2'));
        assert.ok(fetchedAddresses.has('0x3610bad33aac567d2c5fb03e47eec5c2172fd42a'));
    });

    it('handles Sourcify failures gracefully', async () => {
        globalThis.fetch = async () => { throw new Error('ECONNREFUSED'); };

        const ctx = await enrichSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, 1, { cache: memoryCache() });
        assert.equal(ctx.decodedCall, undefined);
        assert.ok(ctx.contracts.size > 0);
    });

    it('skips already-decoded events', async () => {
        mockSourcify({
            '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2': WETH_ABI,
        });

        const ctx = await enrichSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, 1, { cache: memoryCache() });
        assert.ok(ctx.decodedEvents.every(e => e === null));
    });

    it('decodes Error(string) revert reason on failed tx', async () => {
        mockSourcify({});

        const ctx = await enrichSimulation(REVERTED_TX_RESULT, TX_PARAMS, 1, { cache: memoryCache() });
        assert.ok(ctx.decodedError);
        assert.equal(ctx.decodedError.name, 'Error');
        assert.equal(ctx.decodedError.reason, 'Insufficient balance');
    });

    it('does not decode error on successful tx', async () => {
        mockSourcify({});

        const ctx = await enrichSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, 1, { cache: memoryCache() });
        assert.equal(ctx.decodedError, undefined);
    });

    it('does not use EMPTY_CODE_HASH from accessList as a cache key', async () => {
        const emptyHash = '0xc5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470';
        const cache = {
            get: async (key) => {
                assert.equal(key.includes(emptyHash.slice(2)), false,
                    'EMPTY_CODE_HASH must not be used as cache key');
                return null;
            },
            set: async () => { },
        };
        mockSourcify({
            '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2': WETH_ABI,
        });

        const result = {
            ...WETH_DEPOSIT_RESULT,
            accessList: [
                { address: '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2', codeHash: '0x' + 'ab'.repeat(32), storageKeys: [] },
                { address: '0x3610bad33aac567d2c5fb03e47eec5c2172fd42a', codeHash: emptyHash, storageKeys: [] },
            ],
        };
        const ctx = await enrichSimulation(result, TX_PARAMS, 1, { cache });
        assert.ok(ctx.decodedCall);
        assert.equal(ctx.decodedCall.name, 'deposit');
    });

    it('skips Sourcify for addresses with EMPTY_CODE_HASH', async () => {
        const emptyHash = '0xc5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470';
        const eoa = '0x3610bad33aac567d2c5fb03e47eec5c2172fd42a';
        const fetched = [];
        globalThis.fetch = async (url) => {
            const match = url.match(/\/v2\/contract\/\d+\/([^?]+)/);
            if (match) fetched.push(match[1].toLowerCase());
            return new Response(JSON.stringify({
                abi: null, sources: null, compilation: null, stdJsonInput: null,
            }), { status: 200 });
        };

        const result = {
            ...WETH_DEPOSIT_RESULT,
            accessList: [
                { address: eoa, codeHash: emptyHash, storageKeys: [] },
            ],
        };
        await enrichSimulation(result, TX_PARAMS, 1, { cache: memoryCache() });
        assert.equal(fetched.includes(eoa), false);
        assert.ok(fetched.includes('0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2'));
    });

    it('does not refetch unverified addresses on a second enrich (negative cache)', async () => {
        let calls = 0;
        globalThis.fetch = async () => {
            calls++;
            return new Response('Not Found', { status: 404 });
        };
        const cache = memoryCache();

        await enrichSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, 1, { cache });
        const first = calls;
        assert.ok(first > 0);
        await enrichSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, 1, { cache });
        assert.equal(calls, first);
    });

    it('yields the event loop after resolving each contract', async () => {
        const codeHash = '0x' + 'cd'.repeat(32);
        const a = '0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
        const b = '0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';
        const verified = JSON.stringify({
            abi: [],
            storageLayout: null,
            sources: {},
            compilerVersion: '0.8.0',
            contractName: 'C',
        });
        const cache = {
            get: async () => verified,
            set: async () => { },
        };
        let immediates = 0;
        const origImmediate = setImmediate;
        globalThis.setImmediate = (fn, ...args) => {
            immediates++;
            return origImmediate(fn, ...args);
        };
        try {
            const result = {
                gasUsed: '0x1',
                status: '0x1',
                returnValue: '0x',
                logs: [],
                stateChanges: [
                    { address: a, storage: [] },
                    { address: b, storage: [] },
                ],
                accessList: [
                    { address: a, codeHash, storageKeys: [] },
                    { address: b, codeHash, storageKeys: [] },
                ],
            };
            await enrichSimulation(result, { to: a, data: '0xd0e30db0' }, 1, { cache });
            assert.ok(immediates >= 2, `expected a yield per contract, got ${immediates}`);
        } finally {
            globalThis.setImmediate = origImmediate;
        }
    });

    it('resolves two addresses with the same codeHash without throwing', async () => {
        const codeHash = '0x' + 'ab'.repeat(32);
        const a = '0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
        const b = '0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';
        let calls = 0;
        globalThis.fetch = async () => {
            calls++;
            return new Response(JSON.stringify({
                abi: WETH_ABI,
                sources: { 'C.sol': { content: 'pragma solidity ^0.8.0; contract C {}' } },
                compilation: { compilerVersion: '0.8.0', name: 'C' },
                stdJsonInput: null,
            }), { status: 200 });
        };

        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [],
            stateChanges: [
                { address: a, storage: [] },
                { address: b, storage: [] },
            ],
            accessList: [
                { address: a, codeHash, storageKeys: [] },
                { address: b, codeHash, storageKeys: [] },
            ],
        };
        const ctx = await enrichSimulation(result, { to: a, data: '0xd0e30db0' }, 1, { cache: memoryCache() });
        assert.equal(calls, 2);
        assert.deepEqual(ctx.contracts.get(a)?.abi, WETH_ABI);
        assert.deepEqual(ctx.contracts.get(b)?.abi, WETH_ABI);
    });
});

describe('toEnhancedResult', () => {
    it('merges enriched context into the simulation result', async () => {
        resetSourcifyStateForTests();
        setSourcifyClockForTests(() => Date.now(), async () => { });
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
            const ctx = await enrichSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, 1, { cache: memoryCache() });
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
