// Runs the shared fixture suite against the WASM runtime (browser/default entry).

import test from 'node:test';
import { modulePath } from './test_config.js';
import { run_rpc_suite } from './rpc_suite.mjs';

const ColibriModule = await import(modulePath);

test('RPC-Proof Test Suite (wasm)', async (t) => {
    await run_rpc_suite(t, ColibriModule.default);
});
