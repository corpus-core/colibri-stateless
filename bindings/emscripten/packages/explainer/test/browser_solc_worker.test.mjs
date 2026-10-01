import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import { BROWSER_SOLC_WORKER } from '../dist/compiler.js';

/**
 * The browser worker source is a free-standing JS string shipped into
 * `new Worker(Blob)`. These tests load the string into a VM sandbox with
 * stubs for `self`, `importScripts`, and `Blob`, so we can verify the
 * version-dispatch logic without a real soljson binary.
 */
function runWorkerWith(moduleStub, options = {}) {
    const sandbox = {
        self: {},
        URL: { createObjectURL: () => 'blob:dummy', revokeObjectURL: () => { } },
        Blob: function Blob() { return {}; },
        importScripts: () => {
            // The real worker expects `self.Module = ...` to appear here.
            sandbox.self.Module = moduleStub;
        },
    };
    vm.createContext(sandbox);
    vm.runInContext(BROWSER_SOLC_WORKER, sandbox);
    // Invoke the installed onmessage with an `init` message.
    const posted = [];
    sandbox.self.postMessage = (msg) => posted.push(msg);
    sandbox.self.onmessage({ data: { type: 'init', source: 'x'.repeat(options.sourceLength ?? 2000) } });
    return posted;
}

/**
 * Build a Module stub that pretends to be a soljson of a given API generation.
 *
 * @param {object} opts
 * @param {boolean} opts.modern - Expose `_solidity_compile` (0.5.9+).
 * @param {boolean} opts.standard - Expose `_compileStandard` (0.4.11 - 0.5.8).
 * @param {string|undefined} opts.versionName - Which version export is set (`solidity_version`, `version`, or none).
 * @param {boolean} opts.hasSolidityAlloc - Expose `_solidity_alloc`.
 */
function moduleStub(opts) {
    const exports = {};
    if (opts.modern) exports._solidity_compile = () => 0;
    if (opts.standard) exports._compileStandard = () => 0;
    if (opts.versionName === 'solidity_version') exports._solidity_version = () => 0;
    if (opts.versionName === 'version') exports._version = () => 0;
    if (opts.hasSolidityAlloc) exports._solidity_alloc = () => 0;
    // Every real soljson exports malloc.
    exports._malloc = () => 0;

    const wrapped = {};
    return Object.assign(exports, {
        cwrap: (name) => {
            // Mimic old-emscripten cwrap: return a function regardless of
            // existence; crashes only occur when the returned wrapper is
            // invoked. Record cwrap targets so tests can assert the right
            // entry points were picked.
            wrapped[name] = (wrapped[name] ?? 0) + 1;
            if (name === 'version' || name === 'solidity_version') return () => opts.returnedVersion ?? '0.4.19';
            return () => 0;
        },
        addFunction: () => 1,
        removeFunction: () => { },
        UTF8ToString: () => '',
        lengthBytesUTF8: () => 1,
        stringToUTF8: () => { },
        setValue: () => { },
        _wrapped: wrapped,
    });
}

describe('BROWSER_SOLC_WORKER init dispatch', () => {
    it('posts type=ready with the version when _solidity_compile is exported (0.5.9+)', () => {
        const module = moduleStub({ modern: true, versionName: 'solidity_version', hasSolidityAlloc: true, returnedVersion: '0.8.36' });
        const posted = runWorkerWith(module);
        assert.equal(posted.length, 1);
        assert.equal(posted[0].type, 'ready');
        assert.equal(posted[0].version, '0.8.36');
        assert.ok(module._wrapped.solidity_compile, 'must wrap solidity_compile');
        assert.ok(!module._wrapped.compileStandard, 'must not touch compileStandard on modern builds');
    });

    it('posts type=ready and wraps compileStandard for 0.4.11 - 0.5.8 (no _solidity_compile)', () => {
        const module = moduleStub({ standard: true, versionName: 'version', returnedVersion: '0.4.19' });
        const posted = runWorkerWith(module);
        assert.equal(posted.length, 1);
        assert.equal(posted[0].type, 'ready', `expected ready, got ${JSON.stringify(posted[0])}`);
        assert.equal(posted[0].version, '0.4.19');
        assert.ok(module._wrapped.compileStandard, 'must wrap compileStandard on legacy builds');
        assert.ok(!module._wrapped.solidity_compile, 'must not touch solidity_compile on legacy builds');
        assert.ok(module._wrapped.malloc, 'legacy build must fall back to malloc for alloc');
    });

    it('posts type=error with a clean message when neither entry point exists (< 0.4.11)', () => {
        const module = moduleStub({ versionName: 'version', returnedVersion: '0.4.5' });
        const posted = runWorkerWith(module);
        assert.equal(posted.length, 1);
        assert.equal(posted[0].type, 'error');
        assert.ok(posted[0].error.includes('too old'), `expected a "too old" message, got: ${posted[0].error}`);
        assert.ok(posted[0].error.includes('0.4.5'), 'the version should appear in the error for diagnostics');
    });

    it('posts type=error when the soljson blob is too short to be real', () => {
        const module = moduleStub({ modern: true });
        const posted = runWorkerWith(module, { sourceLength: 10 });
        assert.equal(posted[0].type, 'error');
        assert.ok(posted[0].error.includes('invalid soljson source'));
    });
});
