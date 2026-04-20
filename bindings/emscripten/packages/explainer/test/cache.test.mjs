import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { get_default_cache, cacheGet, cacheSet } from '../dist/cache.js';

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
