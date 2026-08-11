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
import { run_rpc_suite } from './rpc_suite.mjs';

process.env.C4_FORCE_NATIVE = '1';

// Local development fallback: use the addon from the default CMake build dir.
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

test('RPC-Proof Test Suite (native)', async (t) => {
    await run_rpc_suite(t, ColibriModule.default);
});
