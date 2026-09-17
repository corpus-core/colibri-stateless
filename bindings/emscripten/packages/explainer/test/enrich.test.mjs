import { describe, it, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import { keccak256, AbiCoder, Interface } from 'ethers';
import { readFileSync } from 'node:fs';
import { enrichSimulation, toEnhancedResult, buildProxyImplementations } from '../dist/enrich.js';
import { resetSourcifyStateForTests, setSourcifyClockForTests } from '../dist/sourcify.js';
import {
    setExplainerLogLevel, setExplainerLogSink, resetExplainerLogForTests,
} from '../dist/log.js';
import { WETH_DEPOSIT_RESULT, TX_PARAMS, WETH_ABI, REVERTED_TX_RESULT, UNI_STORAGE_LAYOUT } from './fixtures.mjs';

function memoryCache() {
    const store = new Map();
    return {
        store,
        get: async (key) => store.get(key) ?? null,
        set: async (key, value) => { store.set(key, value); },
    };
}

describe('buildProxyImplementations', () => {
    const proxy = '0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
    const impl = '0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';
    const lib = '0xcccccccccccccccccccccccccccccccccccccccc';
    const sender = '0x1111111111111111111111111111111111111111';

    it('maps parent CALL to to DELEGATECALL to via traceAddress', () => {
        const map = buildProxyImplementations([
            { type: 'CALL', from: sender, to: proxy, traceAddress: [] },
            { type: 'DELEGATECALL', from: sender, to: impl, traceAddress: [0] },
        ]);
        assert.equal(map.get(proxy), impl);
    });

    it('accepts hex-string traceAddress indices', () => {
        const map = buildProxyImplementations([
            { type: 'CALL', to: proxy, traceAddress: [] },
            { type: 'DELEGATECALL', to: impl, traceAddress: ['0x0'] },
        ]);
        assert.equal(map.get(proxy), impl);
    });

    it('does not map when DELEGATECALL to equals the proxy', () => {
        const map = buildProxyImplementations([
            { type: 'CALL', to: proxy, traceAddress: [] },
            { type: 'DELEGATECALL', to: proxy, traceAddress: [0] },
        ]);
        assert.equal(map.size, 0);
    });

    it('keeps the first implementation and ignores nested library DELEGATECALLs', () => {
        const map = buildProxyImplementations([
            { type: 'CALL', to: proxy, traceAddress: [] },
            { type: 'DELEGATECALL', to: impl, traceAddress: [0] },
            { type: 'DELEGATECALL', to: lib, traceAddress: [0, 0] },
        ]);
        assert.equal(map.get(proxy), impl);
        assert.equal(map.has(impl), false);
    });

    it('maps CALLCODE the same way as DELEGATECALL', () => {
        const map = buildProxyImplementations([
            { type: 'CALL', to: proxy, traceAddress: [] },
            { type: 'CALLCODE', to: impl, traceAddress: [0] },
        ]);
        assert.equal(map.get(proxy), impl);
    });

    it('returns an empty map for missing, empty, or parentless traces', () => {
        assert.equal(buildProxyImplementations(undefined).size, 0);
        assert.equal(buildProxyImplementations([]).size, 0);
        assert.equal(buildProxyImplementations([
            { type: 'DELEGATECALL', to: impl, traceAddress: [0] },
        ]).size, 0);
        assert.equal(buildProxyImplementations([
            { type: 'DELEGATECALL', to: impl, traceAddress: [] },
        ]).size, 0);
    });

    it('matches mixed number and hex traceAddress paths', () => {
        const map = buildProxyImplementations([
            { type: 'CALL', to: proxy, traceAddress: ['0x0'] },
            { type: 'DELEGATECALL', to: impl, traceAddress: [0, '0x1'] },
        ]);
        assert.equal(map.get(proxy), impl);
    });

    it('maps both proxies in the nested state2 trace', () => {
        const sim = JSON.parse(readFileSync(new URL('./data/state2/sim.json', import.meta.url)));
        const map = buildProxyImplementations(sim.trace);
        assert.equal(map.get('0x9e0d578c884768227881b9aa0bd633f62a569bc1'),
            '0xba8d0ceb2cba93a3e281a853059ec9262411dc13');
        assert.equal(map.get('0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48'),
            '0x43506849d7c04f9138d1a2050bbf3a0c054402dd');
        assert.equal(map.size, 2);
    });
});

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
        resetExplainerLogForTests();
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

    it('decodes a proxy call via an access-list implementation ABI', async () => {
        const proxy = '0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
        const impl = '0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';
        const proxyAbi = [
            { name: 'upgradeTo', type: 'function', inputs: [{ name: 'impl', type: 'address' }], outputs: [], stateMutability: 'nonpayable' },
        ];
        const implAbi = [
            {
                name: 'transferFrom', type: 'function',
                inputs: [
                    { name: 'from', type: 'address' },
                    { name: 'to', type: 'address' },
                    { name: 'value', type: 'uint256' },
                ],
                outputs: [{ name: '', type: 'bool' }],
                stateMutability: 'nonpayable',
            },
        ];
        mockSourcify({ [proxy]: proxyAbi, [impl]: implAbi });

        const from = '0x1111111111111111111111111111111111111111';
        const dest = '0x2222222222222222222222222222222222222222';
        const data = new Interface(implAbi).encodeFunctionData('transferFrom', [from, dest, 1n]);
        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [],
            trace: [{ from, to: proxy, input: data, type: 'CALL' }],
            accessList: [
                { address: proxy, codeHash: '0x' + 'aa'.repeat(32), storageKeys: [] },
                { address: impl, codeHash: '0x' + 'bb'.repeat(32), storageKeys: [] },
            ],
        };

        const ctx = await enrichSimulation(result, { to: proxy, from, data }, 1, { cache: memoryCache() });
        assert.ok(ctx.decodedCall);
        assert.equal(ctx.decodedCall.name, 'transferFrom');
        assert.equal(ctx.decodedCall.params[0].value.toLowerCase(), from);
        assert.equal(ctx.decodedTrace[0]?.name, 'transferFrom');
    });

    it('still uses the to-address ABI when the access list has other contracts', async () => {
        const other = '0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';
        const otherAbi = [
            { name: 'upgradeTo', type: 'function', inputs: [{ name: 'impl', type: 'address' }], outputs: [], stateMutability: 'nonpayable' },
        ];
        mockSourcify({
            '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2': WETH_ABI,
            [other]: otherAbi,
        });

        const result = {
            ...WETH_DEPOSIT_RESULT,
            accessList: [
                { address: TX_PARAMS.to, codeHash: '0x' + 'aa'.repeat(32), storageKeys: [] },
                { address: other, codeHash: '0x' + 'bb'.repeat(32), storageKeys: [] },
            ],
        };
        const ctx = await enrichSimulation(result, TX_PARAMS, 1, { cache: memoryCache() });
        assert.equal(ctx.decodedCall?.name, 'deposit');
        assert.equal(ctx.decodedTrace[0]?.name, 'deposit');
    });

    it('decodes a proxy CALL using the DELEGATECALL implementation from the trace', async () => {
        const proxy = '0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
        const impl = '0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';
        const proxyAbi = [
            { name: 'upgradeTo', type: 'function', inputs: [{ name: 'impl', type: 'address' }], outputs: [], stateMutability: 'nonpayable' },
        ];
        const implAbi = [
            {
                name: 'transferFrom', type: 'function',
                inputs: [
                    { name: 'from', type: 'address' },
                    { name: 'to', type: 'address' },
                    { name: 'value', type: 'uint256' },
                ],
                outputs: [{ name: '', type: 'bool' }],
                stateMutability: 'nonpayable',
            },
        ];
        mockSourcify({ [proxy]: proxyAbi, [impl]: implAbi });

        const from = '0x1111111111111111111111111111111111111111';
        const dest = '0x2222222222222222222222222222222222222222';
        const data = new Interface(implAbi).encodeFunctionData('transferFrom', [from, dest, 1n]);
        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [],
            trace: [
                { from, to: proxy, input: data, type: 'CALL', traceAddress: [] },
                { from, to: impl, input: data, type: 'DELEGATECALL', traceAddress: ['0x0'] },
            ],
        };

        const ctx = await enrichSimulation(result, { to: proxy, from, data }, 1, { cache: memoryCache() });
        assert.ok(ctx.decodedCall);
        assert.equal(ctx.decodedCall.name, 'transferFrom');
        assert.equal(ctx.decodedTrace[0]?.name, 'transferFrom');
        assert.equal(ctx.decodedTrace[1]?.name, 'transferFrom');
    });

    it('returns no decoded call when no ABI matches the selector', async () => {
        const proxy = '0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
        const other = '0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';
        mockSourcify({
            [proxy]: [
                { name: 'upgradeTo', type: 'function', inputs: [{ name: 'impl', type: 'address' }], outputs: [], stateMutability: 'nonpayable' },
            ],
            [other]: [
                { name: 'deposit', type: 'function', inputs: [], outputs: [], stateMutability: 'payable' },
            ],
        });

        const from = '0x1111111111111111111111111111111111111111';
        const dest = '0x2222222222222222222222222222222222222222';
        const unknownAbi = [{
            name: 'transferFrom', type: 'function',
            inputs: [
                { name: 'from', type: 'address' },
                { name: 'to', type: 'address' },
                { name: 'value', type: 'uint256' },
            ],
            outputs: [{ name: '', type: 'bool' }],
            stateMutability: 'nonpayable',
        }];
        const data = new Interface(unknownAbi).encodeFunctionData('transferFrom', [from, dest, 1n]);
        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [],
            trace: [{ from, to: proxy, input: data, type: 'CALL' }],
            accessList: [
                { address: proxy, codeHash: '0x' + 'aa'.repeat(32), storageKeys: [] },
                { address: other, codeHash: '0x' + 'bb'.repeat(32), storageKeys: [] },
            ],
        };

        const ctx = await enrichSimulation(result, { to: proxy, from, data }, 1, { cache: memoryCache() });
        assert.equal(ctx.decodedCall, undefined);
        assert.equal(ctx.decodedTrace[0], null);
    });

    it('skips decode when calldata is empty or shorter than a selector', async () => {
        mockSourcify({
            '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2': WETH_ABI,
        });

        const short = {
            ...WETH_DEPOSIT_RESULT,
            trace: [{ ...WETH_DEPOSIT_RESULT.trace[0], input: '0xd0e30d' }],
        };
        const ctxShort = await enrichSimulation(short, { ...TX_PARAMS, data: '0xd0e30d' }, 1, { cache: memoryCache() });
        assert.equal(ctxShort.decodedCall, undefined);
        assert.equal(ctxShort.decodedTrace[0], null);

        const empty = {
            ...WETH_DEPOSIT_RESULT,
            trace: [{ ...WETH_DEPOSIT_RESULT.trace[0], input: '0x' }],
        };
        const ctxEmpty = await enrichSimulation(empty, { ...TX_PARAMS, data: '0x' }, 1, { cache: memoryCache() });
        assert.equal(ctxEmpty.decodedCall, undefined);
        assert.equal(ctxEmpty.decodedTrace[0], null);
    });

    it('decodes a call without to via an access-list ABI', async () => {
        const impl = '0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';
        const implAbi = [
            { name: 'deposit', type: 'function', inputs: [], outputs: [], stateMutability: 'payable' },
        ];
        mockSourcify({ [impl]: implAbi });

        const from = '0x1111111111111111111111111111111111111111';
        const data = '0xd0e30db0';
        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [],
            trace: [{ from, input: data, type: 'CALL' }],
            accessList: [
                { address: impl, codeHash: '0x' + 'bb'.repeat(32), storageKeys: [] },
            ],
        };

        const ctx = await enrichSimulation(result, { from, data }, 1, { cache: memoryCache() });
        assert.equal(ctx.decodedCall?.name, 'deposit');
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

    it('logs a warning when storage layout extraction throws', async () => {
        const addr = '0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
        const longName = 'C' + 'a'.repeat(128);
        globalThis.fetch = async () => new Response(JSON.stringify({
            abi: [],
            sources: {
                'C.sol': { content: `pragma solidity ^0.8.0;\ncontract ${longName} { uint256 public x; }` },
            },
            compilation: { compilerVersion: '0.8.0', name: longName },
            stdJsonInput: null,
        }), { status: 200, headers: { 'Content-Type': 'application/json' } });

        const cache = memoryCache();
        const lines = [];
        setExplainerLogSink((level, message, extra) => { lines.push({ level, message, extra }); });
        setExplainerLogLevel('warn');

        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [],
            stateChanges: [{ address: addr, storage: [] }],
        };
        const tx = { to: addr, data: '0xd0e30db0' };
        const ctx = await enrichSimulation(result, tx, 1, { cache });

        assert.equal(ctx.contracts.get(addr)?.storageLayout, null);
        const hit = lines.find(l => l.level === 'warn' && l.message === 'storage layout extract failed');
        assert.ok(hit, `expected extract-failed warning, got ${JSON.stringify(lines)}`);
        assert.equal(hit.extra.scope, 'enrich');
        assert.equal(hit.extra.address, addr);
        assert.match(String(hit.extra.error), /Invalid Solidity identifier/);
        assert.equal([...cache.store.keys()].filter(k => k.startsWith('c4l_')).length, 0);

        lines.length = 0;
        await enrichSimulation(result, tx, 1, { cache });
        assert.ok(lines.some(l => l.message === 'storage layout extract failed'),
            'failed extractions must not be cached so a later retry still extracts');
        assert.equal([...cache.store.keys()].filter(k => k.startsWith('c4l_')).length, 0);
    });

    it('logs skeleton compile errors when layout extraction returns null', async () => {
        const addr = '0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
        globalThis.fetch = async () => new Response(JSON.stringify({
            abi: [],
            sources: {
                'C.sol': { content: 'pragma solidity ^0.8.0;\ncontract C { UnknownType public x; }' },
            },
            compilation: { compilerVersion: '0.8.0', name: 'C' },
            stdJsonInput: null,
        }), { status: 200, headers: { 'Content-Type': 'application/json' } });

        const cache = memoryCache();
        const lines = [];
        setExplainerLogSink((level, message, extra) => { lines.push({ level, message, extra }); });
        setExplainerLogLevel('warn');

        const ctx = await enrichSimulation({
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [],
            stateChanges: [{ address: addr, storage: [] }],
        }, { to: addr, data: '0xd0e30db0' }, 1, { cache });

        assert.equal(ctx.contracts.get(addr)?.storageLayout, null);
        const hit = lines.find(l => l.level === 'warn' && l.message === 'skeleton compile errors');
        assert.ok(hit, `expected skeleton compile warning, got ${JSON.stringify(lines)}`);
        assert.equal(hit.extra.scope, 'layout');
        assert.equal(hit.extra.contract, 'C');
        assert.ok(String(hit.extra.error).length > 0);
        assert.equal([...cache.store.keys()].filter(k => k.startsWith('c4l_')).length, 0);
    });

    it('caches extracted storage layout and skips solc on the second enrich', async () => {
        const addr = '0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
        const source = 'pragma solidity ^0.8.0;\ncontract C { uint256 public x; mapping(address => uint256) public balances; }';
        globalThis.fetch = async () => new Response(JSON.stringify({
            abi: [{ name: 'x', type: 'function', inputs: [], outputs: [{ type: 'uint256' }], stateMutability: 'view' }],
            sources: { 'C.sol': { content: source } },
            compilation: { compilerVersion: '0.8.0', name: 'C' },
            stdJsonInput: null,
        }), { status: 200, headers: { 'Content-Type': 'application/json' } });

        const store = new Map();
        const cache = {
            get: async (key) => store.get(key) ?? null,
            set: async (key, value) => { store.set(key, value); },
        };
        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [],
            stateChanges: [{
                address: addr,
                storage: [{
                    slot: '0x' + '00'.repeat(32),
                    previousValue: '0x' + '00'.repeat(32),
                    newValue: '0x' + '01'.repeat(32),
                }],
            }],
        };
        const tx = { to: addr, data: '0xd0e30db0' };

        const lines = [];
        setExplainerLogSink((level, message, extra) => { lines.push({ level, message, extra }); });
        setExplainerLogLevel('debug');

        const first = await enrichSimulation(result, tx, 1, { cache });
        assert.equal(first.contracts.get(addr)?.storageLayout?.storage[0]?.label, 'x');
        const layoutKeys = [...store.keys()].filter(k => k.startsWith('c4l_'));
        assert.equal(layoutKeys.length, 1);
        const compileCount = lines.filter(l => l.message === 'compile skeleton').length;
        assert.ok(compileCount >= 1, 'first enrich must compile the skeleton');

        lines.length = 0;
        const second = await enrichSimulation(result, tx, 1, { cache });
        assert.deepEqual(
            second.contracts.get(addr)?.storageLayout,
            first.contracts.get(addr)?.storageLayout,
        );
        assert.equal(lines.some(l => l.message === 'compile skeleton'), false);
        assert.ok(lines.some(l => l.message === 'storage layout cache hit'));
    });

    it('skips already-decoded events', async () => {
        mockSourcify({
            '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2': WETH_ABI,
        });

        const ctx = await enrichSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, 1, { cache: memoryCache() });
        assert.ok(ctx.decodedEvents.every(e => e === null));
    });

    const TRANSFER_EVENT_ABI = [{
        name: 'Transfer', type: 'event',
        inputs: [
            { name: 'from', type: 'address', indexed: true },
            { name: 'to', type: 'address', indexed: true },
            { name: 'value', type: 'uint256', indexed: false },
        ],
        anonymous: false,
    }];
    const PROXY_ADMIN_EVENT_ABI = [{
        name: 'AdminChanged', type: 'event',
        inputs: [
            { name: 'previousAdmin', type: 'address', indexed: false },
            { name: 'newAdmin', type: 'address', indexed: false },
        ],
        anonymous: false,
    }];

    function encodeTransferLog(from, to, value) {
        const iface = new Interface(TRANSFER_EVENT_ABI);
        return iface.encodeEventLog(iface.getEvent('Transfer'), [from, to, value]);
    }

    it('decodes a proxy log via the DELEGATECALL implementation ABI', async () => {
        const proxy = '0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
        const impl = '0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';
        mockSourcify({ [proxy]: PROXY_ADMIN_EVENT_ABI, [impl]: TRANSFER_EVENT_ABI });

        const from = '0x1111111111111111111111111111111111111111';
        const dest = '0x2222222222222222222222222222222222222222';
        const encoded = encodeTransferLog(from, dest, 1n);
        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [{ raw: { address: proxy, topics: encoded.topics, data: encoded.data } }],
            trace: [
                { from, to: proxy, input: '0xd0e30db0', type: 'CALL', traceAddress: [] },
                { from, to: impl, input: '0xd0e30db0', type: 'DELEGATECALL', traceAddress: [0] },
            ],
        };

        const ctx = await enrichSimulation(result, { to: proxy, from, data: '0xd0e30db0' }, 1, { cache: memoryCache() });
        assert.equal(ctx.decodedEvents[0]?.name, 'Transfer');
        assert.equal(ctx.decodedEvents[0]?.params[0].value.toLowerCase(), from);
        assert.equal(ctx.decodedEvents[0]?.params[1].value.toLowerCase(), dest);
    });

    it('decodes a log via another fetched contract ABI when the emitter has none', async () => {
        const proxy = '0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
        const other = '0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';
        mockSourcify({ [proxy]: PROXY_ADMIN_EVENT_ABI, [other]: TRANSFER_EVENT_ABI });

        const from = '0x1111111111111111111111111111111111111111';
        const dest = '0x2222222222222222222222222222222222222222';
        const encoded = encodeTransferLog(from, dest, 1n);
        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [{ raw: { address: proxy, topics: encoded.topics, data: encoded.data } }],
            accessList: [
                { address: proxy, codeHash: '0x' + 'aa'.repeat(32), storageKeys: [] },
                { address: other, codeHash: '0x' + 'bb'.repeat(32), storageKeys: [] },
            ],
        };

        const ctx = await enrichSimulation(result, { to: proxy, from, data: '0xd0e30db0' }, 1, { cache: memoryCache() });
        assert.equal(ctx.decodedEvents[0]?.name, 'Transfer');
    });

    it('prefers the emitter ABI over a mapped implementation for events', async () => {
        const proxy = '0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
        const impl = '0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';
        const proxyTransferAbi = [{
            name: 'Transfer', type: 'event',
            inputs: [
                { name: 'src', type: 'address', indexed: true },
                { name: 'dst', type: 'address', indexed: true },
                { name: 'wad', type: 'uint256', indexed: false },
            ],
            anonymous: false,
        }];
        mockSourcify({ [proxy]: proxyTransferAbi, [impl]: TRANSFER_EVENT_ABI });

        const from = '0x1111111111111111111111111111111111111111';
        const dest = '0x2222222222222222222222222222222222222222';
        const encoded = encodeTransferLog(from, dest, 1n);
        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [{ raw: { address: proxy, topics: encoded.topics, data: encoded.data } }],
            trace: [
                { from, to: proxy, input: '0xd0e30db0', type: 'CALL', traceAddress: [] },
                { from, to: impl, input: '0xd0e30db0', type: 'DELEGATECALL', traceAddress: [0] },
            ],
        };

        const ctx = await enrichSimulation(result, { to: proxy, from, data: '0xd0e30db0' }, 1, { cache: memoryCache() });
        assert.equal(ctx.decodedEvents[0]?.name, 'Transfer');
        assert.equal(ctx.decodedEvents[0]?.params[0].name, 'src');
        assert.equal(ctx.decodedEvents[0]?.params[1].name, 'dst');
    });

    it('returns no decoded event when no ABI matches the topic', async () => {
        const proxy = '0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
        const other = '0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';
        mockSourcify({ [proxy]: PROXY_ADMIN_EVENT_ABI, [other]: PROXY_ADMIN_EVENT_ABI });

        const from = '0x1111111111111111111111111111111111111111';
        const dest = '0x2222222222222222222222222222222222222222';
        const encoded = encodeTransferLog(from, dest, 1n);
        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [{ raw: { address: proxy, topics: encoded.topics, data: encoded.data } }],
            accessList: [
                { address: proxy, codeHash: '0x' + 'aa'.repeat(32), storageKeys: [] },
                { address: other, codeHash: '0x' + 'bb'.repeat(32), storageKeys: [] },
            ],
        };

        const ctx = await enrichSimulation(result, { to: proxy, from, data: '0xd0e30db0' }, 1, { cache: memoryCache() });
        assert.equal(ctx.decodedEvents[0], null);
    });

    it('tries the mapped implementation ABI before other fetched contracts for events', async () => {
        const proxy = '0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
        const impl = '0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';
        const other = '0xcccccccccccccccccccccccccccccccccccccccc';
        const otherTransferAbi = [{
            name: 'Transfer', type: 'event',
            inputs: [
                { name: 'src', type: 'address', indexed: true },
                { name: 'dst', type: 'address', indexed: true },
                { name: 'wad', type: 'uint256', indexed: false },
            ],
            anonymous: false,
        }];
        mockSourcify({
            [proxy]: PROXY_ADMIN_EVENT_ABI,
            [impl]: TRANSFER_EVENT_ABI,
            [other]: otherTransferAbi,
        });

        const from = '0x1111111111111111111111111111111111111111';
        const dest = '0x2222222222222222222222222222222222222222';
        const encoded = encodeTransferLog(from, dest, 1n);
        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [{ raw: { address: proxy, topics: encoded.topics, data: encoded.data } }],
            trace: [
                { from, to: proxy, input: '0xd0e30db0', type: 'CALL', traceAddress: [] },
                { from, to: impl, input: '0xd0e30db0', type: 'DELEGATECALL', traceAddress: [0] },
            ],
        };

        // tx.to is `other` so that ABI is first in the all-contracts scan.
        // Without the implementation step we would decode src/dst from `other`.
        const ctx = await enrichSimulation(result, { to: other, from, data: '0xd0e30db0' }, 1, { cache: memoryCache() });
        assert.equal(ctx.decodedEvents[0]?.name, 'Transfer');
        assert.equal(ctx.decodedEvents[0]?.params[0].name, 'from');
        assert.equal(ctx.decodedEvents[0]?.params[1].name, 'to');
    });

    it('decodes a log without raw.address via another fetched contract ABI', async () => {
        const other = '0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';
        mockSourcify({ [other]: TRANSFER_EVENT_ABI });

        const from = '0x1111111111111111111111111111111111111111';
        const dest = '0x2222222222222222222222222222222222222222';
        const encoded = encodeTransferLog(from, dest, 1n);
        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [{ raw: { topics: encoded.topics, data: encoded.data } }],
            accessList: [
                { address: other, codeHash: '0x' + 'bb'.repeat(32), storageKeys: [] },
            ],
        };

        const ctx = await enrichSimulation(result, { to: other, from, data: '0xd0e30db0' }, 1, { cache: memoryCache() });
        assert.equal(ctx.decodedEvents[0]?.name, 'Transfer');
        assert.equal(ctx.decodedEvents[0]?.params[0].value.toLowerCase(), from);
    });

    it('returns no decoded event for empty topics or missing raw', async () => {
        const proxy = '0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
        mockSourcify({ [proxy]: TRANSFER_EVENT_ABI });

        const from = '0x1111111111111111111111111111111111111111';
        const dest = '0x2222222222222222222222222222222222222222';
        const encoded = encodeTransferLog(from, dest, 1n);
        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [
                { raw: { address: proxy, topics: [], data: encoded.data } },
                { raw: { address: proxy, data: encoded.data } },
                {},
            ],
            trace: [{ from, to: proxy, input: '0xd0e30db0', type: 'CALL' }],
        };

        const ctx = await enrichSimulation(result, { to: proxy, from, data: '0xd0e30db0' }, 1, { cache: memoryCache() });
        assert.equal(ctx.decodedEvents.length, 3);
        assert.ok(ctx.decodedEvents.every(e => e === null));
    });

    it('decodes an event when raw.data is missing', async () => {
        const addr = '0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
        const indexedOnlyAbi = [{
            name: 'Foo', type: 'event',
            inputs: [{ name: 'who', type: 'address', indexed: true }],
            anonymous: false,
        }];
        mockSourcify({ [addr]: indexedOnlyAbi });

        const who = '0x1111111111111111111111111111111111111111';
        const iface = new Interface(indexedOnlyAbi);
        const encoded = iface.encodeEventLog(iface.getEvent('Foo'), [who]);
        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [{ raw: { address: addr, topics: encoded.topics } }],
            trace: [{ from: who, to: addr, input: '0xd0e30db0', type: 'CALL' }],
        };

        const ctx = await enrichSimulation(result, { to: addr, from: who, data: '0xd0e30db0' }, 1, { cache: memoryCache() });
        assert.equal(ctx.decodedEvents[0]?.name, 'Foo');
        assert.equal(ctx.decodedEvents[0]?.params[0].value.toLowerCase(), who);
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

    function nestedAllowanceSlotSource(owner, spender, slot = 3n) {
        const inner = keccak256(AbiCoder.defaultAbiCoder().encode(['address', 'uint256'], [owner, slot]));
        return '0x' + spender.slice(2).padStart(64, '0') + inner.slice(2);
    }

    function layoutCache(codeHash) {
        const verified = JSON.stringify({
            abi: [],
            storageLayout: UNI_STORAGE_LAYOUT,
            sources: {},
            compilerVersion: '0.8.0',
            contractName: 'Uni',
        });
        return {
            get: async (key) => (typeof key === 'string' && key.includes(codeHash) ? verified : null),
            set: async () => { },
        };
    }

    it('resolves nested mapping keys from decoded log address inputs', async () => {
        mockSourcify({});
        const token = '0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
        const owner = '0xedf8a8bf77e25b8a0ebe4a26889fa12f0d5485d5';
        const spender = '0x40aa958dd87fc8305b97f2ba922cddca374bcd7f';
        const codeHash = '0x' + '11'.repeat(32);
        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [{
                inputs: [
                    { name: 'owner', type: 'address', value: owner },
                    { name: 'spender', type: 'address', value: spender },
                    { name: 'value', type: 'uint256', value: '1' },
                ],
                name: 'Approval',
                raw: { address: token, data: '0x', topics: ['0x' + 'ab'.repeat(32)] },
            }],
            stateChanges: [{
                address: token,
                storage: [{
                    slot: '0x' + 'cd'.repeat(32),
                    previousValue: '0x' + '00'.repeat(32),
                    newValue: '0x' + '01'.repeat(32),
                    slotSource: nestedAllowanceSlotSource(owner, spender),
                }],
            }],
            accessList: [{ address: token, codeHash, storageKeys: [] }],
        };

        const ctx = await enrichSimulation(result, { to: token, data: '0xd0e30db0' }, 1, { cache: layoutCache(codeHash) });
        const slots = ctx.resolvedStorage.get(token);
        assert.ok(slots);
        assert.equal(slots[0].variableName, 'allowances');
        assert.equal(slots[0].baseSlot, 3);
        assert.equal(slots[0].keys?.length, 2);
        assert.ok(slots[0].keys[0].value.toLowerCase().includes(owner.slice(2)));
        assert.ok(slots[0].keys[1].value.toLowerCase().includes(spender.slice(2)));
    });

    it('resolves nested mapping keys from indexed address topics', async () => {
        mockSourcify({});
        const token = '0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';
        const owner = '0xedf8a8bf77e25b8a0ebe4a26889fa12f0d5485d5';
        const spender = '0x40aa958dd87fc8305b97f2ba922cddca374bcd7f';
        const codeHash = '0x' + '22'.repeat(32);
        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [{
                raw: {
                    address: token,
                    data: '0x',
                    topics: [
                        '0x' + 'ab'.repeat(32),
                        '0x' + '0'.repeat(24) + owner.slice(2),
                        '0x' + '0'.repeat(24) + spender.slice(2),
                    ],
                },
            }],
            stateChanges: [{
                address: token,
                storage: [{
                    slot: '0x' + 'cd'.repeat(32),
                    previousValue: '0x' + '00'.repeat(32),
                    newValue: '0x' + '01'.repeat(32),
                    slotSource: nestedAllowanceSlotSource(owner, spender),
                }],
            }],
            accessList: [{ address: token, codeHash, storageKeys: [] }],
        };

        const ctx = await enrichSimulation(result, { to: token, data: '0xd0e30db0' }, 1, { cache: layoutCache(codeHash) });
        const slots = ctx.resolvedStorage.get(token);
        assert.ok(slots);
        assert.equal(slots[0].variableName, 'allowances');
        assert.equal(slots[0].keys?.length, 2);
        assert.equal(slots[0].keys[0].value.toLowerCase(), owner);
        assert.equal(slots[0].keys[1].value.toLowerCase(), spender);
    });

    it('resolves proxy storage using the DELEGATECALL implementation layout', async () => {
        mockSourcify({});
        const proxy = '0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
        const impl = '0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';
        const owner = '0xedf8a8bf77e25b8a0ebe4a26889fa12f0d5485d5';
        const spender = '0x40aa958dd87fc8305b97f2ba922cddca374bcd7f';
        const implHash = '0x' + '33'.repeat(32);
        const result = {
            gasUsed: '0x1',
            status: '0x1',
            returnValue: '0x',
            logs: [{
                inputs: [
                    { name: 'owner', type: 'address', value: owner },
                    { name: 'spender', type: 'address', value: spender },
                    { name: 'value', type: 'uint256', value: '1' },
                ],
                name: 'Approval',
                raw: { address: proxy, data: '0x', topics: ['0x' + 'ab'.repeat(32)] },
            }],
            stateChanges: [{
                address: proxy,
                storage: [{
                    slot: '0x' + 'cd'.repeat(32),
                    previousValue: '0x' + '00'.repeat(32),
                    newValue: '0x' + '01'.repeat(32),
                    slotSource: nestedAllowanceSlotSource(owner, spender),
                }],
            }],
            trace: [
                { type: 'CALL', to: proxy, from: owner, input: '0xd0e30db0', traceAddress: [] },
                { type: 'DELEGATECALL', to: impl, from: owner, input: '0xd0e30db0', traceAddress: [0] },
            ],
            accessList: [
                { address: proxy, codeHash: '0x' + '44'.repeat(32), storageKeys: [] },
                { address: impl, codeHash: implHash, storageKeys: [] },
            ],
        };

        const ctx = await enrichSimulation(result, { to: proxy, data: '0xd0e30db0' }, 1, { cache: layoutCache(implHash) });
        const slots = ctx.resolvedStorage.get(proxy);
        assert.ok(slots);
        assert.equal(slots[0].variableName, 'allowances');
        assert.equal(slots[0].keys?.length, 2);
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
