import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import {
    get_default_cache, cacheGet, cacheSet, sanitizeKey,
    sourcifyCompilationKey, sourcifyMetadataKey, getCacheDirectory,
    cacheGetCompilation, cacheSetCompilation,
    cacheGetMetadata, cacheSetMetadata,
    layoutCacheKey,     cacheGetLayout, cacheSetLayout,
    tokenCacheKey, isSafeTokenSymbol, cacheGetToken, cacheSetToken,
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
        assert.equal(sanitizeKey('c4l_0x' + 'ab'.repeat(32)), 'c4l_0x' + 'ab'.repeat(32));
        assert.equal(
            sanitizeKey('c4s_1_0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2'),
            'c4s_1_0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2',
        );
        assert.equal(
            sanitizeKey('c4m_10_0x0000000000000000000000000000000000000001'),
            'c4m_10_0x0000000000000000000000000000000000000001',
        );
        assert.equal(
            sanitizeKey('c4e_1_0x1111111111111111111111111111111111111111'),
            'c4e_1_0x1111111111111111111111111111111111111111',
        );
    });

    it('rejects path traversal and unknown prefixes', () => {
        assert.throws(() => sanitizeKey('c4s_1_../etc/passwd'));
        assert.throws(() => sanitizeKey('c4l_../x'));
        assert.throws(() => sanitizeKey('c4l_deadbeef'));
        assert.throws(() => sanitizeKey('c4l_0xgg'));
        assert.throws(() => sanitizeKey('c4l_0xabc/../../tmp'));
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

describe('cacheGetLayout / cacheSetLayout', () => {
    const sourcesA = { 'A.sol': { content: 'contract A { uint256 public x; }' } };
    const sourcesB = { 'A.sol': { content: 'contract A { uint256 public y; }' } };
    const layout = {
        storage: [{ slot: '0', type: 't_uint256', astId: 1, label: 'x', offset: 0, contract: 'A.sol:A' }],
        types: { t_uint256: { label: 'uint256', encoding: 'inplace', numberOfBytes: '32' } },
    };

    function memoryCache() {
        const store = new Map();
        return {
            store,
            get: async (key) => store.get(key) ?? null,
            set: async (key, value) => { store.set(key, value); },
        };
    }

    it('roundtrips a storage layout and keys by sources fingerprint', async () => {
        const cache = memoryCache();
        assert.equal(await cacheGetLayout(cache, sourcesA, 'A'), null);

        await cacheSetLayout(cache, sourcesA, 'A', layout);
        assert.deepEqual(await cacheGetLayout(cache, sourcesA, 'A'), layout);

        const keys = [...cache.store.keys()];
        assert.equal(keys.length, 1);
        assert.match(keys[0], /^c4l_0x[0-9a-f]{64}$/);

        assert.equal(await cacheGetLayout(cache, sourcesB, 'A'), null);
        assert.equal(await cacheGetLayout(cache, sourcesA, 'B'), null);
        assert.notEqual(layoutCacheKey(sourcesA, 'A'), layoutCacheKey(sourcesB, 'A'));
        assert.notEqual(layoutCacheKey(sourcesA, 'A'), layoutCacheKey(sourcesA, 'B'));
        assert.equal(layoutCacheKey(sourcesA, 'A'), layoutCacheKey({ 'A.sol': sourcesA['A.sol'] }, 'A'));
        assert.equal(layoutCacheKey(sourcesA), layoutCacheKey(sourcesA, ''));
        assert.equal(layoutCacheKey(sourcesA, undefined), layoutCacheKey(sourcesA, ''));

        const unordered = { 'Z.sol': { content: 'z' }, 'A.sol': { content: 'a' } };
        const ordered = { 'A.sol': { content: 'a' }, 'Z.sol': { content: 'z' } };
        assert.equal(layoutCacheKey(unordered, 'A'), layoutCacheKey(ordered, 'A'));
    });

    it('returns null for corrupted or invalid layout entries', async () => {
        const cache = memoryCache();
        const key = layoutCacheKey(sourcesA, 'A');
        await cache.set(key, 'not json {{');
        assert.equal(await cacheGetLayout(cache, sourcesA, 'A'), null);

        await cache.set(key, JSON.stringify({ empty: true }));
        assert.equal(await cacheGetLayout(cache, sourcesA, 'A'), null);
        await cache.set(key, JSON.stringify({ storage: 'not-an-array' }));
        assert.equal(await cacheGetLayout(cache, sourcesA, 'A'), null);
        await cache.set(key, 'null');
        assert.equal(await cacheGetLayout(cache, sourcesA, 'A'), null);
    });

    it('does not persist a layout without a storage array', async () => {
        const cache = memoryCache();
        await cacheSetLayout(cache, sourcesA, 'A', { types: {} });
        await cacheSetLayout(cache, sourcesA, 'A', { storage: null });
        await cacheSetLayout(cache, sourcesA, 'A', { storage: {} });
        assert.equal(cache.store.size, 0);
        assert.equal(await cacheGetLayout(cache, sourcesA, 'A'), null);

        await cacheSetLayout(cache, sourcesA, 'A', { storage: [], types: {} });
        assert.equal(cache.store.size, 1);
        assert.deepEqual(await cacheGetLayout(cache, sourcesA, 'A'), { storage: [], types: {} });
    });
});

describe('cacheGetToken / cacheSetToken', () => {
    const addr = '0x1111111111111111111111111111111111111111';

    function memoryCache() {
        const store = new Map();
        return {
            store,
            get: async (key) => store.get(key) ?? null,
            set: async (key, value) => { store.set(key, value); },
        };
    }

    it('roundtrips a safe symbol and rejects unsafe input', async () => {
        const cache = memoryCache();
        assert.equal(tokenCacheKey(1, addr), `c4e_1_${addr}`);
        assert.equal(isSafeTokenSymbol('USDC'), true);
        assert.equal(isSafeTokenSymbol('bad symbol'), false);
        assert.equal(isSafeTokenSymbol('a\nb'), false);

        await cacheSetToken(cache, 1, addr, { symbol: 'USDC', decimals: 6 });
        assert.deepEqual(await cacheGetToken(cache, 1, addr), { symbol: 'USDC', decimals: 6 });

        await cacheSetToken(cache, 1, addr, { symbol: 'bad symbol', decimals: 6 });
        assert.deepEqual(await cacheGetToken(cache, 1, addr), { symbol: 'USDC', decimals: 6 });

        await cacheSetToken(cache, 1, addr, { symbol: 'DAI', decimals: 256 });
        assert.deepEqual(await cacheGetToken(cache, 1, addr), { symbol: 'USDC', decimals: 6 });
    });

    it('returns null for a corrupted token entry', async () => {
        const cache = memoryCache();
        cache.store.set(`c4e_1_${addr}`, 'not json {{');
        assert.equal(await cacheGetToken(cache, 1, addr), null);
    });

    it('rejects invalid keys and unsafe cached shapes', async () => {
        const cache = memoryCache();
        assert.equal(tokenCacheKey(-1, addr), null);
        assert.equal(tokenCacheKey(1.5, addr), null);
        assert.equal(tokenCacheKey(1, 'not-an-address'), null);
        assert.equal(tokenCacheKey(1, '0xzz'), null);

        assert.equal(isSafeTokenSymbol(''), false);
        assert.equal(isSafeTokenSymbol('A'.repeat(33)), false);
        assert.equal(isSafeTokenSymbol('USD.C'), true);
        assert.equal(isSafeTokenSymbol('A$'), true);
        assert.equal(isSafeTokenSymbol('x-y'), true);

        await cacheSetToken(cache, 1, addr, { symbol: 'OK', decimals: -1 });
        assert.equal(cache.store.size, 0);

        cache.store.set(`c4e_1_${addr}`, JSON.stringify({ symbol: 'USDC' }));
        assert.equal(await cacheGetToken(cache, 1, addr), null);

        cache.store.set(`c4e_1_${addr}`, JSON.stringify({ symbol: 'bad symbol', decimals: 6 }));
        assert.equal(await cacheGetToken(cache, 1, addr), null);

        cache.store.set(`c4e_1_${addr}`, JSON.stringify({ symbol: 'USDC', decimals: 6.5 }));
        assert.equal(await cacheGetToken(cache, 1, addr), null);
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

/**
 * Install a minimal `globalThis.caches` + `Response` for one test. Node 18+
 * ships `Response` via undici; the Cache API is browser-only, so we stub it
 * with a Map-backed implementation.
 *
 * @param options - `putThrows: true` triggers a QuotaExceeded on `put`
 * @return `{ store, restore }` -- the backing map + a teardown function
 */
function withCacheApiMock(options = {}) {
    const store = new Map();
    const prevCaches = Object.getOwnPropertyDescriptor(globalThis, 'caches');
    const fakeCache = {
        match: async (key) => {
            const value = store.get(String(key));
            if (value === undefined) return undefined;
            return new Response(value, { headers: { 'content-type': 'application/json' } });
        },
        put: async (key, response) => {
            if (options.putThrows) {
                const err = new Error('Quota exceeded');
                err.name = 'QuotaExceededError';
                throw err;
            }
            const body = await response.text();
            store.set(String(key), body);
        },
        delete: async (key) => store.delete(String(key)),
    };
    const fakeCaches = { open: async () => fakeCache };
    Object.defineProperty(globalThis, 'caches', {
        configurable: true, enumerable: true, writable: true, value: fakeCaches,
    });
    return {
        store,
        restore: () => {
            if (prevCaches) Object.defineProperty(globalThis, 'caches', prevCaches);
            else delete globalThis.caches;
        },
    };
}

describe('Cache Storage API backend', () => {
    it('getDefaultCache prefers caches over the filesystem fallback', async () => {
        const { store, restore } = withCacheApiMock();
        try {
            const cache = await get_default_cache();
            await cache.set('c4x_0xaabbccdd', '{"v":1}');
            // Key goes through sanitizeKey and gets wrapped in a `https://c4-explainer.local/` URL.
            const keys = [...store.keys()];
            assert.equal(keys.length, 1);
            assert.ok(keys[0].startsWith('https://c4-explainer.local/'));
            assert.ok(keys[0].endsWith('c4x_0xaabbccdd'));
            assert.equal(await cache.get('c4x_0xaabbccdd'), '{"v":1}');
        } finally {
            restore();
        }
    });

    it('returns null for missing keys', async () => {
        const { restore } = withCacheApiMock();
        try {
            const cache = await get_default_cache();
            assert.equal(await cache.get('c4x_0xdeadbeef'), null);
        } finally {
            restore();
        }
    });

    it('silently drops QuotaExceededError on set (does not throw)', async () => {
        const { store, restore } = withCacheApiMock({ putThrows: true });
        try {
            const cache = await get_default_cache();
            // Must not throw -- a lost write only means the next read misses.
            await cache.set('c4x_0xaabbccdd', '{"v":1}');
            assert.equal(store.size, 0);
            assert.equal(await cache.get('c4x_0xaabbccdd'), null);
        } finally {
            restore();
        }
    });

    it('invalid cache keys are rejected (sanitizeKey throws, swallowed by set)', async () => {
        const { store, restore } = withCacheApiMock();
        try {
            const cache = await get_default_cache();
            await cache.set('evil_../etc/passwd', 'x');
            assert.equal(store.size, 0, 'invalid key must not reach the cache');
            assert.equal(await cache.get('evil_../etc/passwd'), null);
        } finally {
            restore();
        }
    });

    it('reclaims orphaned c4*_ entries from legacy localStorage once', async () => {
        // Install a Map-backed localStorage with a mix of legacy entries and
        // an unrelated key owned by another app.
        const lsMap = new Map();
        lsMap.set('c4x_0xaaaa', 'legacy-verified');
        lsMap.set('c4s_1_0xdead', 'legacy-sourcify');
        lsMap.set('c4e_1_0xbeef', 'legacy-erc20');
        lsMap.set('user-pref', 'keep-me');
        const fakeLs = {
            get length() { return lsMap.size; },
            key: (i) => [...lsMap.keys()][i] ?? null,
            getItem: (k) => (lsMap.has(k) ? lsMap.get(k) : null),
            setItem: (k, v) => { lsMap.set(k, v); },
            removeItem: (k) => { lsMap.delete(k); },
        };
        const prevLs = Object.getOwnPropertyDescriptor(globalThis, 'localStorage');
        Object.defineProperty(globalThis, 'localStorage', {
            configurable: true, enumerable: true, writable: true, value: fakeLs,
        });
        const { restore } = withCacheApiMock();
        try {
            await get_default_cache();
            // All legacy c4*_ entries must be gone; unrelated keys stay.
            assert.equal(lsMap.has('c4x_0xaaaa'), false);
            assert.equal(lsMap.has('c4s_1_0xdead'), false);
            assert.equal(lsMap.has('c4e_1_0xbeef'), false);
            assert.equal(lsMap.get('user-pref'), 'keep-me');
        } finally {
            restore();
            if (prevLs) Object.defineProperty(globalThis, 'localStorage', prevLs);
            else delete globalThis.localStorage;
        }
    });
});
