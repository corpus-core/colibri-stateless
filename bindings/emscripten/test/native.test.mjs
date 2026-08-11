// Runs the shared fixture suite against the native Node.js addon
// (index.node.js entry with C4_FORCE_NATIVE, no WASM fallback).
//
// Resolution order for the addon binary:
// 1. C4_NATIVE_ADDON env var (explicit path, used by local dev builds)
// 2. prebuilds/<platform>-<arch>/colibri_native.node next to the package output (CI)

import test from 'node:test';
import assert from 'node:assert';
import * as fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { modulePath } from './test_config.js';
import { run_rpc_suite, setup_fixture } from './rpc_suite.mjs';

process.env.C4_FORCE_NATIVE = '1';

// Local development fallback: use the addon from the default CMake build dir
// (build dir `build/node-addon` + output subdir `node-addon`, hence the
// repeated path segment).
const __dirname = dirname(fileURLToPath(import.meta.url));
if (!process.env.C4_NATIVE_ADDON) {
    const local = join(__dirname, '../../../build/node-addon/node-addon/colibri_native.node');
    if (fs.existsSync(local)) process.env.C4_NATIVE_ADDON = local;
}

const ColibriModule = await import(modulePath.replace(/index\.js$/, 'index.node.js'));

test('native addon is active', async () => {
    const runtime = await ColibriModule.getRuntime();
    assert.strictEqual(runtime.kind, 'native', 'expected the native addon runtime');
});

test('addon error paths', async (t) => {
    const runtime = await ColibriModule.getRuntime();
    const proverArgs = ['eth_getBalance', '["0x0000000000000000000000000000000000000000","latest"]', 1n, 0];

    await t.test('double free throws cleanly', () => {
        const ctx = runtime.createProverCtx(...proverArgs);
        runtime.freeProverCtx(ctx);
        // the second free must not crash or double-free, but throw a TypeError
        assert.throws(() => runtime.freeProverCtx(ctx), /invalid or already freed/);
    });

    await t.test('use after free throws', () => {
        const ctx = runtime.createProverCtx(...proverArgs);
        runtime.freeProverCtx(ctx);
        assert.throws(() => runtime.executeProverCtx(ctx), /invalid or already freed/);
    });

    await t.test('wrong handle kind throws', () => {
        const ctx = runtime.createProverCtx(...proverArgs);
        assert.throws(() => runtime.verifyProof(ctx), /invalid or already freed/);
        runtime.freeProverCtx(ctx);
    });

    await t.test('non-handle argument throws', () => {
        assert.throws(() => runtime.executeProverCtx({}), /expected a native context handle/);
        assert.throws(() => runtime.freeRpcCtx(42), /expected a native context handle/);
    });

    await t.test('invalid argument types throw', () => {
        assert.throws(() => runtime.createProverCtx(42, '[]', 1n, 0), /expected a string/);
        assert.throws(() => runtime.getMethodType(NaN, 'eth_chainId', null, 0), /out of uint64 range/);
        assert.throws(() => runtime.getMethodType(-1, 'eth_chainId', null, 0), /out of uint64 range/);
        assert.throws(() => runtime.getMethodType(2n ** 64n, 'eth_chainId', null, 0), /out of uint64 range/);
    });

    await t.test('createVerifyCtx rejects empty method', () => {
        assert.throws(() => runtime.createVerifyCtx(new Uint8Array([1]), '', '[]', 1n, null, null, 0, 0n), /method cannot be empty/);
    });

    await t.test('createVerifyCtx surfaces init errors', () => {
        // args must be a JSON array -> c4_verify_init fails, no handle is created
        assert.throws(() => runtime.createVerifyCtx(new Uint8Array(0), 'eth_chainId', '{}', 1n, null, null, 0, 0n), /args must be a JSON array/);
    });

    await t.test('leading whitespace in args does not corrupt the heap', () => {
        // json_parse skips leading whitespace, so verify.args.start points into
        // the buffer; freeing the ctx must free the original allocation.
        const ctx = runtime.createVerifyCtx(new Uint8Array(0), 'eth_chainId', '  []', 1n, null, null, 0, 0n);
        runtime.freeVerifyCtx(ctx);
    });
});

test('throwing storage callbacks are treated as misses', async () => {
    const Colibri = ColibriModule.default;

    // Create a valid proof with working fixture storage first.
    const { test_conf, conf } = setup_fixture(Colibri, 'eth_getBalance1');
    const proof = await new Colibri(conf).createProof(test_conf.method, test_conf.params);
    assert.ok(proof.length > 0);

    // Then verify with storage callbacks that always throw: the addon must
    // clear the JS exception and treat every access as a miss. Verification
    // may then fail in an orderly way (missing sync state -> data requests
    // the fixture cache cannot answer) but must not crash the addon.
    Colibri.register_storage({
        get: () => { throw new Error('storage boom') },
        set: () => { throw new Error('storage boom') },
        del: () => { throw new Error('storage boom') },
    });
    try {
        await new Colibri(conf).verifyProof(test_conf.method, test_conf.params, proof);
    } catch (e) {
        assert.ok(e instanceof Error, 'expected an orderly Error');
    }

    // The addon must still be fully functional afterwards (no pending exception).
    Colibri.register_storage({ get: () => null, set: () => { }, del: () => { } });
    const c4 = new Colibri();
    assert.strictEqual(await c4.getMethodSupport('eth_getTransactionByHash'), 1);
});

test('RPC-Proof Test Suite (native)', async (t) => {
    await run_rpc_suite(t, ColibriModule.default, ColibriModule.decode_proof);
});
