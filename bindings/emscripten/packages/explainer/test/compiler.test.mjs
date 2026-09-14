import { describe, it, after } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync, existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { Module } from 'node:module';
import { getBundledCompiler, compileAndVerify, loadCompiler, solcCacheFileName, resetCompilerStateForTests, hashRuntimeBytecode, compileSoljsonSourceForTests, installDummyCompilerForTests, compilerCacheSizeForTests } from '../dist/compiler.js';
import { keccak256 } from 'ethers';

describe('getBundledCompiler', () => {
    it('returns a compiler with compile and version methods', async () => {
        const compiler = await getBundledCompiler();
        assert.ok(typeof compiler.compile === 'function');
        assert.ok(typeof compiler.version === 'function');
        assert.ok(compiler.version().startsWith('0.8.'));
    });

    it('compiles a simple contract', async () => {
        const compiler = await getBundledCompiler();
        const input = JSON.stringify({
            language: 'Solidity',
            sources: { 'test.sol': { content: 'pragma solidity >=0.8.0; contract T { uint256 public x; }' } },
            settings: { outputSelection: { '*': { '*': ['abi', 'evm.deployedBytecode.object'] } } },
        });

        const output = JSON.parse(compiler.compile(input));
        assert.ok(output.contracts);
        assert.ok(output.contracts['test.sol']['T'].abi);
        assert.ok(output.contracts['test.sol']['T'].evm.deployedBytecode.object);
    });
});

describe('compileAndVerify', () => {
    it('verifies matching bytecode against codeHash', async () => {
        const compiler = await getBundledCompiler();
        const source = 'pragma solidity >=0.8.0; contract T { uint256 public x; }';

        const input = JSON.stringify({
            language: 'Solidity',
            sources: { 'test.sol': { content: source } },
            settings: { outputSelection: { '*': { '*': ['evm.deployedBytecode.object'] } } },
        });

        const output = JSON.parse(compiler.compile(input));
        const bytecode = output.contracts['test.sol']['T'].evm.deployedBytecode.object;
        const expectedHash = keccak256('0x' + bytecode);

        const stdJsonInput = {
            language: 'Solidity',
            sources: { 'test.sol': { content: source } },
            settings: { outputSelection: { '*': { '*': ['abi'] } } },
        };

        const result = await compileAndVerify(
            stdJsonInput,
            compiler.version(),
            expectedHash,
            { 'test.sol': { content: source } },
        );

        assert.equal(result.verified, true);
        assert.ok(Array.isArray(result.abi));
        assert.ok(result.sources);
    });

    it('returns verified=false for mismatched codeHash', async () => {
        const compiler = await getBundledCompiler();
        const source = 'pragma solidity >=0.8.0; contract T { uint256 public x; }';

        const stdJsonInput = {
            language: 'Solidity',
            sources: { 'test.sol': { content: source } },
            settings: { outputSelection: { '*': { '*': ['abi'] } } },
        };

        const result = await compileAndVerify(
            stdJsonInput,
            compiler.version(),
            '0x0000000000000000000000000000000000000000000000000000000000000000',
            { 'test.sol': { content: source } },
        );

        assert.equal(result.verified, false);
    });

    it('returns verified=false for empty sources', async () => {
        const compiler = await getBundledCompiler();
        const result = await compileAndVerify(
            { language: 'Solidity', sources: {}, settings: {} },
            compiler.version(),
            '0x1234',
            {},
        );

        assert.equal(result.verified, false);
    });
});

describe('hashRuntimeBytecode', () => {
    it('hashes clean hex with or without 0x', () => {
        const a = hashRuntimeBytecode('00');
        const b = hashRuntimeBytecode('0x00');
        assert.equal(a, b);
        assert.ok(a.startsWith('0x'));
        assert.equal(a.length, 66);
    });

    it('returns null for placeholders and invalid hex', () => {
        assert.equal(hashRuntimeBytecode('0x-36ece5c4'), null);
        assert.equal(hashRuntimeBytecode('73__$aabbccdd$__'), null);
        assert.equal(hashRuntimeBytecode('0x__LibName_________________'), null);
        assert.equal(hashRuntimeBytecode('0xabc'), null);
        assert.equal(hashRuntimeBytecode(''), null);
    });
});

describe('solc disk cache', () => {
    const previousDir = process.env.C4_STATE_DIR;
    const tmp = mkdtempSync(join(tmpdir(), 'c4x-solc-'));

    after(() => {
        resetCompilerStateForTests();
        rmSync(tmp, { recursive: true, force: true });
        if (previousDir === undefined) delete process.env.C4_STATE_DIR;
        else process.env.C4_STATE_DIR = previousDir;
    });

    it('builds a safe filename from a validated version', () => {
        assert.equal(solcCacheFileName('0.4.26+commit.4563c3fc'), 'soljson-v0.4.26+commit.4563c3fc.js');
        assert.equal(solcCacheFileName('v0.8.19+commit.7dd6d404'), 'soljson-v0.8.19+commit.7dd6d404.js');
        assert.equal(
            solcCacheFileName('0.8.19+commit.7dd6d404.Emscripten.clang'),
            'soljson-v0.8.19+commit.7dd6d404.Emscripten.clang.js',
        );
        assert.throws(() => solcCacheFileName('../evil'));
        assert.throws(() => solcCacheFileName('0.8.19'));
        assert.throws(() => solcCacheFileName('v0.8.19+commit.7dd6d404/../x'));
    });

    it('persists a downloaded soljson file under C4_STATE_DIR/solc/wasm', async () => {
        process.env.C4_STATE_DIR = tmp;
        resetCompilerStateForTests();
        const originalFetch = globalThis.fetch;
        globalThis.fetch = async (url) => {
            assert.ok(String(url).includes('/wasm/soljson-v0.4.26+commit.4563c3fc.js'));
            return new Response('not-a-real-solc-binary', { status: 200 });
        };
        try {
            await assert.rejects(() => loadCompiler('0.4.26+commit.4563c3fc'));
            const file = join(tmp, 'solc', 'wasm', 'soljson-v0.4.26+commit.4563c3fc.js');
            assert.equal(existsSync(file), true);
            assert.equal(readFileSync(file, 'utf-8'), 'not-a-real-solc-binary');
        } finally {
            globalThis.fetch = originalFetch;
        }
    });

    it('falls back to /bin/ when the wasm build is missing', async () => {
        process.env.C4_STATE_DIR = tmp;
        resetCompilerStateForTests();
        const originalFetch = globalThis.fetch;
        const urls = [];
        globalThis.fetch = async (url) => {
            urls.push(String(url));
            if (String(url).includes('/wasm/')) {
                return new Response('missing', { status: 404 });
            }
            return new Response('asmjs-fallback', { status: 200 });
        };
        try {
            await assert.rejects(() => loadCompiler('0.4.11+commit.68ef5810'));
            assert.ok(urls.some(u => u.includes('/wasm/')));
            assert.ok(urls.some(u => u.includes('/bin/')));
            const file = join(tmp, 'solc', 'wasm', 'soljson-v0.4.11+commit.68ef5810.js');
            assert.equal(readFileSync(file, 'utf-8'), 'asmjs-fallback');
        } finally {
            globalThis.fetch = originalFetch;
        }
    });

    it('shares one in-flight download for the same version', async () => {
        process.env.C4_STATE_DIR = tmp;
        resetCompilerStateForTests();
        let calls = 0;
        let release;
        const gate = new Promise(resolve => { release = resolve; });
        const originalFetch = globalThis.fetch;
        globalThis.fetch = async () => {
            calls++;
            await gate;
            return new Response('not-a-real-solc-binary', { status: 200 });
        };
        try {
            const p1 = loadCompiler('0.5.17+commit.d19bba13');
            const p2 = loadCompiler('0.5.17+commit.d19bba13');
            release();
            await Promise.allSettled([p1, p2]);
            assert.equal(calls, 1);
        } finally {
            globalThis.fetch = originalFetch;
        }
    });
});

describe('solc module release', () => {
    after(() => {
        resetCompilerStateForTests();
    });

    it('drops process listeners added by a compiled module', async () => {
        const beforeRejection = process.listenerCount('unhandledRejection');
        const beforeException = process.listenerCount('uncaughtException');
        const loaded = await compileSoljsonSourceForTests(
            `
            process.on('unhandledRejection', function c4xFakeRejection() {});
            process.on('uncaughtException', function c4xFakeException() {});
            module.exports = { ok: true };
            `,
            'soljson-c4x-listener-test.js',
        );
        assert.deepEqual(loaded.exports, { ok: true });
        assert.ok(process.listenerCount('unhandledRejection') > beforeRejection);
        assert.ok(process.listenerCount('uncaughtException') > beforeException);
        loaded.release();
        assert.equal(process.listenerCount('unhandledRejection'), beforeRejection);
        assert.equal(process.listenerCount('uncaughtException'), beforeException);
    });

    it('evicts oldest dummy compilers and calls release', () => {
        resetCompilerStateForTests();
        const released = [];
        for (let i = 0; i < 5; i++) {
            installDummyCompilerForTests(`0.8.${i}+commit.aaaaaa`, () => released.push(i));
        }
        assert.equal(compilerCacheSizeForTests(), 3);
        assert.deepEqual(released, [0, 1]);
        resetCompilerStateForTests();
        assert.equal(compilerCacheSizeForTests(), 0);
        assert.deepEqual(released, [0, 1, 2, 3, 4]);
    });

    it('loadCompiler returns the cached compiler, not the LRU wrapper', async () => {
        resetCompilerStateForTests();
        installDummyCompilerForTests('0.8.99+commit.bbbbbb', () => { });
        const compiler = await loadCompiler('0.8.99+commit.bbbbbb');
        assert.equal(typeof compiler.compile, 'function');
        assert.equal(compiler.compile('{}'), '{}');
        assert.equal(compiler.version(), '0.8.99+commit.bbbbbb');
        assert.equal(compilerCacheSizeForTests(), 1);
    });

    it('drops listeners even when _compile throws', async () => {
        const events = ['uncaughtException', 'unhandledRejection', 'exit', 'beforeExit', 'warning'];
        const before = Object.fromEntries(events.map(e => [e, process.listenerCount(e)]));
        await assert.rejects(
            () => compileSoljsonSourceForTests(
                `
                process.on('uncaughtException', function c4xFailException() {});
                process.on('unhandledRejection', function c4xFailRejection() {});
                process.on('exit', function c4xFailExit() {});
                process.on('beforeExit', function c4xFailBeforeExit() {});
                process.on('warning', function c4xFailWarning() {});
                throw new Error('soljson compile boom');
                `,
                'soljson-c4x-compile-fail.js',
            ),
            /soljson compile boom/,
        );
        for (const event of events) {
            assert.equal(process.listenerCount(event), before[event], event);
        }
        assert.equal('soljson-c4x-compile-fail.js' in (Module._cache || {}), false);
    });

    it('release does not remove listeners that existed before compile', async () => {
        function keepWarning() { }
        process.on('warning', keepWarning);
        try {
            const loaded = await compileSoljsonSourceForTests(
                `process.on('warning', function c4xExtraWarning() {}); module.exports = { ok: true };`,
                'soljson-c4x-keep-listener.js',
            );
            loaded.release();
            assert.ok(process.listeners('warning').includes(keepWarning));
        } finally {
            process.removeListener('warning', keepWarning);
        }
    });

    it('release of one module does not drop later host or sibling listeners', async () => {
        function hostAfter() { }
        const first = await compileSoljsonSourceForTests(
            `process.on('warning', function c4xFirstWarning() {}); module.exports = { n: 1 };`,
            'soljson-c4x-first.js',
        );
        process.on('warning', hostAfter);
        const second = await compileSoljsonSourceForTests(
            `process.on('warning', function c4xSecondWarning() {}); module.exports = { n: 2 };`,
            'soljson-c4x-second.js',
        );
        try {
            first.release();
            assert.ok(process.listeners('warning').includes(hostAfter));
            assert.ok(process.listeners('warning').some(fn => fn.name === 'c4xSecondWarning'));
            assert.equal(process.listeners('warning').some(fn => fn.name === 'c4xFirstWarning'), false);
        } finally {
            second.release();
            process.removeListener('warning', hostAfter);
        }
    });
});
