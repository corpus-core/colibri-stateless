import { describe, it, after } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, rmSync, existsSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { Module } from 'node:module';
import { Worker } from 'node:worker_threads';
import { getBundledCompiler, compileAndVerify, loadCompiler, solcCacheFileName, resetCompilerStateForTests, hashRuntimeBytecode, compileSoljsonSourceForTests, installDummyCompilerForTests, compilerCacheSizeForTests } from '../dist/compiler.js';
import { keccak256 } from 'ethers';

const FAKE_SOLC_V1 = '0.7.6+commit.aaaaaa';
const FAKE_SOLC_V2 = '0.7.7+commit.bbbbbb';

/**
 * Minimal CommonJS soljson that `solc.setupMethods` can wrap.
 *
 * @param version - Short compiler version string
 * @return Fake soljson JavaScript source
 */
function fakeSoljsonSource(version) {
    return `
        process.on('unhandledRejection', function c4xFakeSolcReject() {});
        module.exports = {
            cwrap: function (name) {
                if (name === 'version' || name === 'solidity_version') {
                    return function () {
                        process.on('uncaughtException', function c4xFakeSolcVersion() {});
                        return ${JSON.stringify(version)};
                    };
                }
                if (name === 'solidity_compile' || name === 'compileStandard') {
                    return function (input) {
                        process.once('warning', function c4xFakeSolcCompile() {});
                        if (typeof input === 'string' && input.indexOf('__c4xThrow') !== -1) {
                            throw new Error('compile boom');
                        }
                        if (typeof input === 'string' && input.indexOf('__c4xHang') !== -1) {
                            Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0);
                        }
                        return JSON.stringify({
                            contracts: {
                                'test.sol': {
                                    T: {
                                        abi: [{ type: 'function', name: 'x' }],
                                        evm: { deployedBytecode: { object: '00' } }
                                    }
                                }
                            }
                        });
                    };
                }
                if (name === 'solidity_reset') return function () {};
                return function () {};
            },
            _version: 1,
            _solidity_compile: 1,
            _solidity_reset: 1,
            addFunction: function () { return 1; },
            removeFunction: function () {},
            UTF8ToString: function () { return ''; },
            lengthBytesUTF8: function (s) { return String(s).length; },
            stringToUTF8: function () {},
            setValue: function () {},
            _malloc: function () { return 1; }
        };
    `;
}

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

    it('uses compileAsync when the cached compiler provides it', async () => {
        resetCompilerStateForTests();
        installDummyCompilerForTests('0.8.99+commit.bbbbbb', () => { });
        const compiler = await loadCompiler('0.8.99+commit.bbbbbb');
        let asyncCalls = 0;
        const bytecode = 'aabb';
        compiler.compileAsync = async () => {
            asyncCalls++;
            return JSON.stringify({
                contracts: {
                    'test.sol': {
                        T: {
                            abi: [{ type: 'function', name: 'x' }],
                            evm: { deployedBytecode: { object: bytecode } },
                        },
                    },
                },
            });
        };

        const result = await compileAndVerify(
            { language: 'Solidity', sources: {}, settings: {} },
            '0.8.99+commit.bbbbbb',
            keccak256('0x' + bytecode),
            { 'test.sol': { content: '' } },
        );
        assert.equal(asyncCalls, 1);
        assert.equal(result.verified, true);
        assert.ok(Array.isArray(result.abi));
        resetCompilerStateForTests();
    });

    it('returns verified=false when compileAsync throws', async () => {
        resetCompilerStateForTests();
        installDummyCompilerForTests('0.8.99+commit.cccccc', () => { });
        const compiler = await loadCompiler('0.8.99+commit.cccccc');
        compiler.compileAsync = async () => { throw new Error('worker down'); };

        const result = await compileAndVerify(
            { language: 'Solidity', sources: {}, settings: {} },
            '0.8.99+commit.cccccc',
            keccak256('0x00'),
            {},
        );
        assert.equal(result.verified, false);
        resetCompilerStateForTests();
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
        const origOn = process.on;
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
        assert.equal(process.on, origOn);
        assert.ok(process.listenerCount('unhandledRejection') > beforeRejection);
        assert.ok(process.listenerCount('uncaughtException') > beforeException);
        loaded.release();
        assert.equal(process.listenerCount('unhandledRejection'), beforeRejection);
        assert.equal(process.listenerCount('uncaughtException'), beforeException);
    });

    it('captures addListener, once, prependListener, and prependOnceListener', async () => {
        const origOn = process.on;
        const origAdd = process.addListener;
        const origOnce = process.once;
        const origPrepend = process.prependListener;
        const origPrependOnce = process.prependOnceListener;
        const events = ['uncaughtException', 'unhandledRejection', 'exit', 'warning'];
        const before = Object.fromEntries(events.map(e => [e, process.listenerCount(e)]));
        const loaded = await compileSoljsonSourceForTests(
            `
            process.addListener('uncaughtException', function c4xAddExc() {});
            process.once('unhandledRejection', function c4xOnceRej() {});
            process.prependListener('exit', function c4xPrependExit() {});
            process.prependOnceListener('warning', function c4xPrependOnceWarn() {});
            module.exports = { ok: true };
            `,
            'soljson-c4x-on-methods.js',
        );
        assert.equal(process.on, origOn);
        assert.equal(process.addListener, origAdd);
        assert.equal(process.once, origOnce);
        assert.equal(process.prependListener, origPrepend);
        assert.equal(process.prependOnceListener, origPrependOnce);
        for (const event of events) {
            assert.ok(process.listenerCount(event) > before[event], event);
        }
        loaded.release();
        for (const event of events) {
            assert.equal(process.listenerCount(event), before[event], event);
        }
    });

    it('evicts oldest dummy compilers and calls release', () => {
        resetCompilerStateForTests();
        const released = [];
        for (let i = 0; i < 5; i++) {
            installDummyCompilerForTests(`0.8.${i}+commit.aaaaaa`, () => released.push(i));
        }
        assert.equal(compilerCacheSizeForTests(), 1);
        assert.deepEqual(released, [0, 1, 2, 3]);
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

describe('remote solc worker', () => {
    const previousDir = process.env.C4_STATE_DIR;
    const previousInProcess = process.env.C4_SOLC_IN_PROCESS;
    const tmp = mkdtempSync(join(tmpdir(), 'c4x-solc-worker-'));

    after(() => {
        resetCompilerStateForTests();
        rmSync(tmp, { recursive: true, force: true });
        if (previousDir === undefined) delete process.env.C4_STATE_DIR;
        else process.env.C4_STATE_DIR = previousDir;
        if (previousInProcess === undefined) delete process.env.C4_SOLC_IN_PROCESS;
        else process.env.C4_SOLC_IN_PROCESS = previousInProcess;
    });

    /**
     * Write a fake soljson into the disk cache and load it.
     *
     * @param version - Compiler version (no leading `v`)
     * @param inProcess - When true, skip the worker (`C4_SOLC_IN_PROCESS=1`)
     * @return Loaded compiler instance
     */
    async function loadFake(version, inProcess = false) {
        process.env.C4_STATE_DIR = tmp;
        if (inProcess) process.env.C4_SOLC_IN_PROCESS = '1';
        else delete process.env.C4_SOLC_IN_PROCESS;
        const file = join(tmp, 'solc', 'wasm', solcCacheFileName(version));
        mkdirSync(join(tmp, 'solc', 'wasm'), { recursive: true });
        writeFileSync(file, fakeSoljsonSource(version));
        return loadCompiler(version);
    }

    it('compileAsync compiles in a worker and compile() is rejected', async () => {
        resetCompilerStateForTests();
        const compiler = await loadFake(FAKE_SOLC_V1);
        assert.equal(typeof compiler.compileAsync, 'function');
        assert.throws(() => compiler.compile('{}'), /compileAsync/);
        const raw = await compiler.compileAsync('{}');
        const output = JSON.parse(raw);
        assert.ok(output.contracts['test.sol'].T.abi);
        assert.equal(compilerCacheSizeForTests(), 1);
    });

    it('compileAndVerify uses worker compileAsync for a matching hash', async () => {
        resetCompilerStateForTests();
        await loadFake(FAKE_SOLC_V1);
        const result = await compileAndVerify(
            { language: 'Solidity', sources: { 'test.sol': { content: 'x' } }, settings: {} },
            FAKE_SOLC_V1,
            keccak256('0x00'),
            { 'test.sol': { content: 'x' } },
        );
        assert.equal(result.verified, true);
        assert.ok(Array.isArray(result.abi));
    });

    it('rejects compileAsync when the worker compile throws', async () => {
        resetCompilerStateForTests();
        const compiler = await loadFake(FAKE_SOLC_V1);
        await assert.rejects(
            () => compiler.compileAsync(JSON.stringify({ __c4xThrow: true })),
            /compile boom/,
        );
    });

    it('rejects in-flight compileAsync when the worker is released', async () => {
        resetCompilerStateForTests();
        const compiler = await loadFake(FAKE_SOLC_V1);
        const hanging = compiler.compileAsync(JSON.stringify({ __c4xHang: true }));
        resetCompilerStateForTests();
        await assert.rejects(() => hanging, /compiler worker released/);
    });

    it('evicts the previous worker when loading a second version', async () => {
        resetCompilerStateForTests();
        const first = await loadFake(FAKE_SOLC_V1);
        const hanging = first.compileAsync(JSON.stringify({ __c4xHang: true }));
        const second = await loadFake(FAKE_SOLC_V2);
        assert.equal(compilerCacheSizeForTests(), 1);
        await assert.rejects(() => hanging, /compiler worker released/);
        const raw = await second.compileAsync('{}');
        assert.ok(JSON.parse(raw).contracts);
    });

    it('in-process fallback captures listeners during version and compile', async () => {
        resetCompilerStateForTests();
        const origOn = process.on;
        const beforeRejection = process.listenerCount('unhandledRejection');
        const beforeException = process.listenerCount('uncaughtException');
        const beforeWarning = process.listenerCount('warning');
        const compiler = await loadFake(FAKE_SOLC_V1, true);
        assert.equal(typeof compiler.compileAsync, 'undefined');
        assert.equal(process.on, origOn);
        assert.ok(process.listenerCount('unhandledRejection') > beforeRejection);
        assert.ok(process.listenerCount('uncaughtException') > beforeException);
        compiler.compile('{}');
        assert.ok(process.listenerCount('warning') > beforeWarning);
        assert.equal(process.on, origOn);
        resetCompilerStateForTests();
        assert.equal(process.listenerCount('unhandledRejection'), beforeRejection);
        assert.equal(process.listenerCount('uncaughtException'), beforeException);
        assert.equal(process.listenerCount('warning'), beforeWarning);
    });

    it('worker posts error when soljson source is missing', async () => {
        const worker = new Worker(new URL('../dist/compiler-worker.js', import.meta.url), {
            workerData: { filename: 'soljson-missing.js' },
        });
        try {
            const msg = await new Promise((resolve, reject) => {
                worker.on('message', resolve);
                worker.on('error', reject);
            });
            assert.equal(msg.type, 'error');
            assert.match(String(msg.error), /missing soljson source/);
        } finally {
            await worker.terminate();
        }
    });

    it('refuses to run as a regular module', async () => {
        await assert.rejects(
            () => import('../dist/compiler-worker.js'),
            /compiler-worker must run as a worker thread/,
        );
    });
});
