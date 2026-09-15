import { describe, it, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import {
    fetchContractMetadata, fetchCompilationInput,
    resetSourcifyStateForTests, setSourcifyClockForTests, parseRetryAfter,
    setSourcifyLogger,
} from '../dist/sourcify.js';
import {
    setExplainerLogLevel, setExplainerLogSink, resetExplainerLogForTests,
} from '../dist/log.js';

const ADDR = '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2';

function memoryCache() {
    const store = new Map();
    return {
        store,
        get: async (key) => store.get(key) ?? null,
        set: async (key, value) => { store.set(key, value); },
    };
}

describe('fetchContractMetadata', () => {
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

    it('returns parsed metadata for a verified contract', async () => {
        globalThis.fetch = async (url) => {
            assert.ok(url.includes('/v2/contract/1/0xabc'));
            assert.ok(url.includes('fields=abi,sources,storageLayout'));
            return new Response(JSON.stringify({
                abi: [{ name: 'transfer', type: 'function' }],
                sources: { 'Token.sol': { content: 'pragma solidity ^0.8.0;' } },
                storageLayout: {
                    storage: [{ slot: '0', label: 'totalSupply', type: 't_uint256' }],
                    types: { t_uint256: { label: 'uint256', encoding: 'inplace', numberOfBytes: '32' } },
                },
            }), { status: 200, headers: { 'Content-Type': 'application/json' } });
        };

        const meta = await fetchContractMetadata('0xabc', 1);
        assert.ok(Array.isArray(meta.abi));
        assert.equal(meta.abi.length, 1);
        assert.ok(meta.sources);
        assert.equal(meta.sources['Token.sol'].content, 'pragma solidity ^0.8.0;');
        assert.ok(meta.storageLayout);
        assert.equal(meta.storageLayout.storage[0].label, 'totalSupply');
    });

    it('returns null fields for unverified contracts (404)', async () => {
        globalThis.fetch = async () => new Response('Not Found', { status: 404 });

        const meta = await fetchContractMetadata('0xdead', 1);
        assert.equal(meta.abi, null);
        assert.equal(meta.sources, null);
        assert.equal(meta.storageLayout, null);
    });

    it('returns null fields when storageLayout is null (old compiler)', async () => {
        globalThis.fetch = async () => new Response(JSON.stringify({
            abi: [{ name: 'deposit', type: 'function' }],
            sources: { 'WETH.sol': { content: 'contract WETH {}' } },
            storageLayout: null,
        }), { status: 200, headers: { 'Content-Type': 'application/json' } });

        const meta = await fetchContractMetadata('0xweth', 1);
        assert.ok(meta.abi);
        assert.ok(meta.sources);
        assert.equal(meta.storageLayout, null);
    });

    it('handles network errors gracefully', async () => {
        globalThis.fetch = async () => { throw new Error('ECONNREFUSED'); };

        const meta = await fetchContractMetadata('0xabc', 1);
        assert.equal(meta.abi, null);
        assert.equal(meta.sources, null);
        assert.equal(meta.storageLayout, null);
    });

    it('uses custom baseUrl', async () => {
        globalThis.fetch = async (url) => {
            assert.ok(url.startsWith('https://my-sourcify.example/v2/'));
            return new Response(JSON.stringify({ abi: [], sources: null, storageLayout: null }), {
                status: 200, headers: { 'Content-Type': 'application/json' },
            });
        };

        const meta = await fetchContractMetadata('0xabc', 1, 'https://my-sourcify.example');
        assert.ok(meta);
    });

    it('rejects empty storageLayout storage arrays', async () => {
        globalThis.fetch = async () => new Response(JSON.stringify({
            abi: [],
            sources: null,
            storageLayout: { storage: [], types: null },
        }), { status: 200, headers: { 'Content-Type': 'application/json' } });

        const meta = await fetchContractMetadata('0xrouter', 1);
        assert.equal(meta.storageLayout, null);
    });
});

describe('Sourcify cache and rate limits', () => {
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

    it('persists 404 as a miss and skips the second fetch', async () => {
        let calls = 0;
        globalThis.fetch = async () => {
            calls++;
            return new Response('Not Found', { status: 404 });
        };
        const cache = memoryCache();

        await fetchCompilationInput(ADDR, 1, undefined, cache);
        await fetchCompilationInput(ADDR, 1, undefined, cache);
        assert.equal(calls, 1);
        assert.ok([...cache.store.keys()].some(k => k.startsWith('c4s_1_')));
    });

    it('does not persist 429 as a miss', async () => {
        let calls = 0;
        globalThis.fetch = async () => {
            calls++;
            return new Response('slow down', { status: 429, headers: { 'Retry-After': '1' } });
        };
        const cache = memoryCache();

        await fetchCompilationInput(ADDR, 1, undefined, cache);
        assert.equal(cache.store.size, 0);
        assert.ok(calls >= 2);
    });

    it('retries 429 with Retry-After then succeeds', async () => {
        let calls = 0;
        const sleeps = [];
        let now = 1_000_000;
        setSourcifyClockForTests(() => now, async (ms) => { now += ms; sleeps.push(ms); });
        globalThis.fetch = async () => {
            calls++;
            if (calls === 1) {
                return new Response('slow down', { status: 429, headers: { 'Retry-After': '1' } });
            }
            return new Response(JSON.stringify({
                abi: [{ name: 'deposit', type: 'function' }],
                sources: { 'WETH.sol': { content: 'contract WETH {}' } },
                compilation: { compilerVersion: '0.8.19+commit.7dd6d404', name: 'WETH' },
                stdJsonInput: { language: 'Solidity' },
            }), { status: 200, headers: { 'Content-Type': 'application/json' } });
        };

        const cache = memoryCache();
        const result = await fetchCompilationInput(ADDR, 1, undefined, cache);
        assert.equal(calls, 2);
        assert.deepEqual(sleeps, [1000]);
        assert.equal(result.contractName, 'WETH');
        assert.equal(await cache.get(`c4s_1_${ADDR}`).then(v => JSON.parse(v).contractName), 'WETH');
    });

    it('shares a cooldown across parallel calls', async () => {
        let calls = 0;
        const sleeps = [];
        setSourcifyClockForTests(() => 1_000_000, async (ms) => { sleeps.push(ms); });
        globalThis.fetch = async () => {
            calls++;
            return new Response('slow down', { status: 429, headers: { 'Retry-After': '2' } });
        };

        const addr2 = '0x0000000000000000000000000000000000000001';
        await Promise.all([
            fetchCompilationInput(ADDR, 1),
            fetchCompilationInput(addr2, 1),
        ]);

        assert.ok(sleeps.length > 0);
        assert.ok(sleeps.every(ms => ms === 2000));
        assert.ok(calls <= 10);
    });

    it('deduplicates in-flight fetches for the same address', async () => {
        let calls = 0;
        let release;
        const gate = new Promise(resolve => { release = resolve; });
        globalThis.fetch = async () => {
            calls++;
            await gate;
            return new Response(JSON.stringify({
                abi: [], sources: null, compilation: null, stdJsonInput: null,
            }), { status: 200 });
        };

        const p1 = fetchCompilationInput(ADDR, 1);
        const p2 = fetchCompilationInput(ADDR, 1);
        release();
        await Promise.all([p1, p2]);
        assert.equal(calls, 1);
    });

    it('persists metadata 404 as a miss and skips the second fetch', async () => {
        let calls = 0;
        globalThis.fetch = async () => {
            calls++;
            return new Response('Not Found', { status: 404 });
        };
        const cache = memoryCache();

        await fetchContractMetadata(ADDR, 1, undefined, cache);
        await fetchContractMetadata(ADDR, 1, undefined, cache);
        assert.equal(calls, 1);
        assert.ok([...cache.store.keys()].some(k => k.startsWith('c4m_1_')));
        assert.equal(JSON.parse(cache.store.get(`c4m_1_${ADDR}`)).empty, true);
    });

    it('does not persist 503 as a miss', async () => {
        let calls = 0;
        globalThis.fetch = async () => {
            calls++;
            return new Response('unavailable', { status: 503, headers: { 'Retry-After': '1' } });
        };
        const cache = memoryCache();

        await fetchCompilationInput(ADDR, 1, undefined, cache);
        assert.equal(cache.store.size, 0);
        assert.equal(calls, 5);
    });

    it('does not persist network errors as a miss and backs off exponentially', async () => {
        const originalRandom = Math.random;
        Math.random = () => 1;
        let now = 1_000_000;
        const sleeps = [];
        setSourcifyClockForTests(() => now, async (ms) => { now += ms; sleeps.push(ms); });
        globalThis.fetch = async () => { throw new Error('ECONNREFUSED'); };
        const cache = memoryCache();
        try {
            await fetchCompilationInput(ADDR, 1, undefined, cache);
            assert.equal(cache.store.size, 0);
            assert.deepEqual(sleeps, [1000, 2000, 4000, 8000]);
        } finally {
            Math.random = originalRandom;
        }
    });

    it('clamps Retry-After on 503 retries then succeeds without treating 503 as a miss', async () => {
        let now = 1_000_000;
        const sleeps = [];
        setSourcifyClockForTests(() => now, async (ms) => { now += ms; sleeps.push(ms); });
        let calls = 0;
        globalThis.fetch = async () => {
            calls++;
            if (calls === 1) {
                return new Response('unavailable', { status: 503, headers: { 'Retry-After': '120' } });
            }
            return new Response(JSON.stringify({
                abi: [{ name: 'deposit', type: 'function' }],
                sources: { 'WETH.sol': { content: 'contract WETH {}' } },
                compilation: { compilerVersion: '0.8.19+commit.7dd6d404', name: 'WETH' },
                stdJsonInput: { language: 'Solidity' },
            }), { status: 200, headers: { 'Content-Type': 'application/json' } });
        };
        const cache = memoryCache();
        const result = await fetchCompilationInput(ADDR, 1, undefined, cache);
        assert.equal(calls, 2);
        assert.deepEqual(sleeps, [60000]);
        assert.equal(result.contractName, 'WETH');
        assert.equal(JSON.parse(cache.store.get(`c4s_1_${ADDR}`)).empty, undefined);
    });

    it('caches an empty 200 compilation as a miss', async () => {
        let calls = 0;
        globalThis.fetch = async () => {
            calls++;
            return new Response(JSON.stringify({
                abi: null, sources: null, compilation: null, stdJsonInput: null,
            }), { status: 200 });
        };
        const cache = memoryCache();
        await fetchCompilationInput(ADDR, 1, undefined, cache);
        await fetchCompilationInput(ADDR, 1, undefined, cache);
        assert.equal(calls, 1);
        assert.equal(JSON.parse(cache.store.get(`c4s_1_${ADDR}`)).empty, true);
    });

    it('does not persist HTTP 200 with invalid JSON as a miss', async () => {
        let calls = 0;
        globalThis.fetch = async () => {
            calls++;
            return new Response('<html>gateway</html>', { status: 200, headers: { 'Content-Type': 'text/html' } });
        };
        const cache = memoryCache();
        const result = await fetchCompilationInput(ADDR, 1, undefined, cache);
        assert.equal(result.abi, null);
        assert.equal(cache.store.size, 0);
        assert.equal(calls, 1);
    });

    it('waits for the global cooldown before a later request', async () => {
        const sleeps = [];
        setSourcifyClockForTests(() => 1_000_000, async (ms) => { sleeps.push(ms); });
        let calls = 0;
        globalThis.fetch = async () => {
            calls++;
            if (calls === 1) {
                return new Response('slow down', { status: 429, headers: { 'Retry-After': '5' } });
            }
            return new Response(JSON.stringify({
                abi: null, sources: null, compilation: null, stdJsonInput: null,
            }), { status: 200 });
        };

        await fetchCompilationInput(ADDR, 1);
        const sleepsAfterFirst = sleeps.length;
        assert.ok(sleepsAfterFirst >= 1);
        assert.ok(sleeps.every(ms => ms === 5000));
        await fetchCompilationInput('0x0000000000000000000000000000000000000002', 1);
        assert.equal(sleeps[sleepsAfterFirst], 5000);
        assert.equal(calls, 3);
    });

    it('caps concurrent Sourcify fetches at 4', async () => {
        let inFlight = 0;
        let maxInFlight = 0;
        let release;
        const gate = new Promise(resolve => { release = resolve; });
        globalThis.fetch = async () => {
            inFlight++;
            maxInFlight = Math.max(maxInFlight, inFlight);
            await gate;
            inFlight--;
            return new Response(JSON.stringify({
                abi: null, sources: null, compilation: null, stdJsonInput: null,
            }), { status: 200 });
        };

        const addrs = [];
        for (let i = 1; i <= 6; i++) {
            addrs.push('0x' + i.toString(16).padStart(40, '0'));
        }
        const pending = addrs.map(addr => fetchCompilationInput(addr, 1));
        for (let i = 0; i < 20 && maxInFlight < 4; i++) {
            await new Promise(resolve => setImmediate(resolve));
        }
        assert.equal(maxInFlight, 4);
        release();
        await Promise.all(pending);
        assert.equal(maxInFlight, 4);
    });
});

describe('parseRetryAfter', () => {
    it('parses integer seconds and clamps to 1s–60s', () => {
        assert.equal(parseRetryAfter('1'), 1000);
        assert.equal(parseRetryAfter('2'), 2000);
        assert.equal(parseRetryAfter('0'), 1000);
        assert.equal(parseRetryAfter('120'), 60000);
    });

    it('parses HTTP-date values relative to now and clamps', () => {
        const now = Date.parse('Wed, 21 Oct 2015 07:28:00 GMT');
        setSourcifyClockForTests(() => now);
        try {
            assert.equal(parseRetryAfter('Wed, 21 Oct 2015 07:28:05 GMT'), 5000);
            assert.equal(parseRetryAfter('Wed, 21 Oct 2015 07:28:00 GMT'), 1000);
            assert.equal(parseRetryAfter('Wed, 21 Oct 2015 09:28:00 GMT'), 60000);
        } finally {
            resetSourcifyStateForTests();
        }
    });

    it('returns null for missing or unusable values', () => {
        assert.equal(parseRetryAfter(null), null);
        assert.equal(parseRetryAfter(''), null);
        assert.equal(parseRetryAfter('not-a-date'), null);
    });
});

describe('Sourcify error logging', () => {
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

    it('logs network failures after retries are exhausted', async () => {
        const logs = [];
        setSourcifyLogger((message, extra) => { logs.push({ message, extra }); });
        globalThis.fetch = async () => { throw new Error('ECONNREFUSED'); };

        await fetchContractMetadata('0xabc', 1);
        assert.ok(logs.some(l => l.message.includes('request failed')));
        assert.ok(logs.some(l => String(l.extra?.error || '').includes('ECONNREFUSED')));
    });

    it('logs non-404 HTTP errors and does not log 404', async () => {
        const logs = [];
        setSourcifyLogger((message, extra) => { logs.push({ message, extra }); });
        globalThis.fetch = async () => new Response('nope', { status: 500 });

        await fetchCompilationInput(ADDR, 1);
        assert.ok(logs.some(l => l.message.includes('HTTP 500')));

        logs.length = 0;
        globalThis.fetch = async () => new Response('Not Found', { status: 404 });
        await fetchCompilationInput('0x0000000000000000000000000000000000000001', 1);
        assert.equal(logs.length, 0);
    });

    it('logs invalid JSON on HTTP 200', async () => {
        const logs = [];
        setSourcifyLogger((message) => { logs.push(message); });
        globalThis.fetch = async () => new Response('not-json', { status: 200 });

        await fetchContractMetadata('0xabc', 1);
        assert.ok(logs.some(m => m.includes('invalid JSON')));
    });

    it('logs when HTTP 200 JSON is not an object', async () => {
        const logs = [];
        setSourcifyLogger((message) => { logs.push(message); });
        globalThis.fetch = async () => new Response('[]', { status: 200, headers: { 'Content-Type': 'application/json' } });

        const meta = await fetchContractMetadata('0xabc', 1);
        assert.equal(meta.abi, null);
        assert.ok(logs.some(m => m.includes('not a JSON object')));
    });

    it('logs HTTP retries then a terminal status', async () => {
        const logs = [];
        setSourcifyLogger((message, extra) => { logs.push({ message, extra }); });
        let calls = 0;
        globalThis.fetch = async () => {
            calls++;
            if (calls === 1) return new Response('slow down', { status: 429, headers: { 'Retry-After': '1' } });
            return new Response('nope', { status: 500 });
        };

        await fetchCompilationInput(ADDR, 1);
        assert.ok(logs.some(l => l.message.includes('HTTP 429') && l.message.includes('retrying')));
        assert.ok(logs.some(l => l.message.includes('HTTP 500') && !l.message.includes('retrying')));
        assert.equal(logs.some(l => l.extra?.status === 404), false);
    });

    it('does not fail the request when the logger throws', async () => {
        setSourcifyLogger(() => { throw new Error('logger down'); });
        globalThis.fetch = async () => { throw new Error('ECONNREFUSED'); };
        const meta = await fetchContractMetadata('0xabc', 1);
        assert.equal(meta.abi, null);
    });

    it('silences logs when the logger is set to null', async () => {
        const logs = [];
        const explainerLines = [];
        setSourcifyLogger(null);
        setExplainerLogSink((level, message) => { explainerLines.push({ level, message }); });
        setExplainerLogLevel('debug');
        const origWarn = console.warn;
        console.warn = (...args) => { logs.push(args); };
        try {
            globalThis.fetch = async () => new Response('nope', { status: 500 });
            await fetchCompilationInput(ADDR, 1);
            assert.equal(logs.length, 0);
            assert.equal(explainerLines.some(l => l.message.includes('HTTP 500')), false);
        } finally {
            console.warn = origWarn;
        }
    });

    it('does not double-print sourcify errors through explainerLog when a custom logger is set', async () => {
        const sourcifyLogs = [];
        const explainerLines = [];
        setSourcifyLogger((message, extra) => { sourcifyLogs.push({ message, extra }); });
        setExplainerLogSink((level, message, extra) => {
            explainerLines.push({ level, message, extra });
        });
        setExplainerLogLevel('debug');
        globalThis.fetch = async () => { throw new Error('ECONNREFUSED'); };

        await fetchContractMetadata('0xabc', 1);
        assert.ok(sourcifyLogs.some(l => l.message.includes('request failed')));
        assert.equal(
            explainerLines.some(l => l.level === 'warn' && String(l.message).includes('request failed')),
            false,
        );
    });

    it('routes sourcify errors through explainerLog when no custom logger is set', async () => {
        const explainerLines = [];
        setExplainerLogSink((level, message, extra) => {
            explainerLines.push({ level, message, extra });
        });
        setExplainerLogLevel('warn');
        globalThis.fetch = async () => new Response('nope', { status: 500 });

        await fetchCompilationInput(ADDR, 1);
        const hit = explainerLines.find(l => String(l.message).includes('HTTP 500'));
        assert.ok(hit);
        assert.equal(hit.level, 'warn');
        assert.equal(hit.extra.scope, 'sourcify');
    });

    it('stringifies non-Error network failures', async () => {
        const logs = [];
        setSourcifyLogger((message, extra) => { logs.push({ message, extra }); });
        globalThis.fetch = async () => { throw 'ECONNREFUSED'; };

        await fetchContractMetadata('0xabc', 1);
        assert.ok(logs.some(l => String(l.extra?.error || '').includes('ECONNREFUSED')));
    });
});
