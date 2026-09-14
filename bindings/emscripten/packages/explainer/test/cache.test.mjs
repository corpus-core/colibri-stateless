import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import {
    get_default_cache, cacheGet, cacheSet, sanitizeKey,
    sourcifyCompilationKey, sourcifyMetadataKey, getCacheDirectory,
    cacheGetCompilation, cacheSetCompilation,
    cacheGetMetadata, cacheSetMetadata,
} from '../dist/cache.js';

describe('get_default_cache', () => {
    it('returns a cache with get and set methods', async () => {
        const cache = await get_default_cache();
        assert.ok(typeof cache.get === 'function');
        assert.ok(typeof cache.set === 'function');
    });

    it('returns null for missing keys', async () => {
        const cache = await get_default_cache();
        const result = await cache.get('c4x_0xdeadbeefdeadbeef');
        assert.equal(result, null);
    });

    it('stores and retrieves values', async () => {
        const cache = await get_default_cache();
        await cache.set('c4x_0xaabbccdd11223344', '{"hello":"world"}');
        const result = await cache.get('c4x_0xaabbccdd11223344');
        assert.equal(result, '{"hello":"world"}');
    });
});

describe('cacheGet / cacheSet', () => {
    it('roundtrips a VerifiedContract through the cache', async () => {
        const cache = await get_default_cache();
        const contract = {
            abi: [{ name: 'transfer', type: 'function' }],
            storageLayout: { storage: [], types: {} },
            sources: { 'Test.sol': { content: 'pragma solidity ^0.8.0;' } },
            compilerVersion: '0.8.19',
            contractName: 'Test',
        };

        await cacheSet(cache, '0xabcdef1234567890', contract);
        const retrieved = await cacheGet(cache, '0xabcdef1234567890');

        assert.deepEqual(retrieved, contract);
    });

    it('returns null for uncached codeHash', async () => {
        const cache = await get_default_cache();
        const result = await cacheGet(cache, '0x0000000000000000');
        assert.equal(result, null);
    });

    it('returns null for corrupted cache entries', async () => {
        const cache = await get_default_cache();
        await cache.set('c4x_0xbadcafe000000001', 'not valid json {{');
        const result = await cacheGet(cache, '0xbadcafe000000001');
        assert.equal(result, null);
    });

    it('works with custom cache implementation', async () => {
        const store = new Map();
        const custom = {
            get: async (key) => store.get(key) ?? null,
            set: async (key, value) => { store.set(key, value); },
        };

        const contract = {
            abi: [],
            storageLayout: null,
            sources: {},
            compilerVersion: '0.8.0',
            contractName: 'X',
        };

        await cacheSet(custom, '0xdeadbeef', contract);
        assert.ok(store.has('c4x_0xdeadbeef'));

        const retrieved = await cacheGet(custom, '0xdeadbeef');
        assert.deepEqual(retrieved, contract);
    });
});

describe('sanitizeKey / sourcify keys', () => {
    it('accepts verified-contract and sourcify prefixes', () => {
        assert.equal(sanitizeKey('c4x_0xdeadbeef'), 'c4x_0xdeadbeef');
        assert.equal(
            sanitizeKey('c4s_1_0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2'),
            'c4s_1_0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2',
        );
        assert.equal(
            sanitizeKey('c4m_10_0x0000000000000000000000000000000000000001'),
            'c4m_10_0x0000000000000000000000000000000000000001',
        );
    });

    it('rejects path traversal and unknown prefixes', () => {
        assert.throws(() => sanitizeKey('c4s_1_../etc/passwd'));
        assert.throws(() => sanitizeKey('c4x_../x'));
        assert.throws(() => sanitizeKey('c4s_1_0x../aabb'));
        assert.throws(() => sanitizeKey('c4s_1_0xabc/../etc/passwd'));
        assert.throws(() => sanitizeKey('c4m_1_0xabc\\x'));
        assert.throws(() => sanitizeKey('c4x_0xabc/../../tmp'));
        assert.throws(() => sanitizeKey('evil_0xabc'));
    });

    it('builds lowercase sourcify keys and rejects invalid addresses', () => {
        assert.equal(
            sourcifyCompilationKey(1, '0xC02aaA39b223FE8D0A0e5C4F27eAD9083C756Cc2'),
            'c4s_1_0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2',
        );
        assert.equal(
            sourcifyMetadataKey(10, '0x0000000000000000000000000000000000000001'),
            'c4m_10_0x0000000000000000000000000000000000000001',
        );
        assert.equal(sourcifyMetadataKey(1, '0xnot-hex'), null);
        assert.equal(sourcifyCompilationKey(-1, '0xabc'), null);
        assert.equal(sourcifyCompilationKey(0x100000000, '0xabc'), null);
    });
});

describe('cacheGetCompilation / cacheSetCompilation', () => {
    it('roundtrips a compilation input and a miss marker', async () => {
        const store = new Map();
        const cache = {
            get: async (key) => store.get(key) ?? null,
            set: async (key, value) => { store.set(key, value); },
        };
        const addr = '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2';
        const input = {
            stdJsonInput: { language: 'Solidity' },
            compilerVersion: '0.8.19+commit.7dd6d404',
            contractName: 'WETH',
            abi: [],
            sources: { 'WETH.sol': { content: 'contract WETH {}' } },
        };

        await cacheSetCompilation(cache, 1, addr, input);
        assert.deepEqual(await cacheGetCompilation(cache, 1, addr), input);

        await cacheSetCompilation(cache, 1, addr, 'empty');
        assert.equal(await cacheGetCompilation(cache, 1, addr), 'empty');
    });
});

describe('cacheGetMetadata / cacheSetMetadata', () => {
    it('roundtrips metadata and a miss marker', async () => {
        const store = new Map();
        const cache = {
            get: async (key) => store.get(key) ?? null,
            set: async (key, value) => { store.set(key, value); },
        };
        const addr = '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2';
        const meta = {
            abi: [{ name: 'deposit', type: 'function' }],
            sources: { 'WETH.sol': { content: 'contract WETH {}' } },
            storageLayout: { storage: [{ slot: '0', label: 'x', type: 't_uint256' }], types: {} },
        };

        await cacheSetMetadata(cache, 1, addr, meta);
        assert.deepEqual(await cacheGetMetadata(cache, 1, addr), meta);
        assert.ok([...store.keys()].every(k => k.startsWith('c4m_1_')));

        await cacheSetMetadata(cache, 1, addr, 'empty');
        assert.equal(await cacheGetMetadata(cache, 1, addr), 'empty');
    });

    it('returns null for corrupted metadata entries', async () => {
        const store = new Map();
        const cache = {
            get: async (key) => store.get(key) ?? null,
            set: async (key, value) => { store.set(key, value); },
        };
        const addr = '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2';
        store.set('c4m_1_' + addr, 'not json {{');
        assert.equal(await cacheGetMetadata(cache, 1, addr), null);
    });
});

describe('getCacheDirectory', () => {
    it('honours C4_STATE_DIR', async () => {
        const previous = process.env.C4_STATE_DIR;
        process.env.C4_STATE_DIR = '/tmp/c4x-cache-test';
        try {
            assert.equal(await getCacheDirectory(), '/tmp/c4x-cache-test');
        } finally {
            if (previous === undefined) delete process.env.C4_STATE_DIR;
            else process.env.C4_STATE_DIR = previous;
        }
    });
});
