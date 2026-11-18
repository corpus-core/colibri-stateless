// End-to-end sanity test for the `chain_configs` client option and the
// underlying `configureChain` runtime hook. Runs against the WASM runtime.

import test from 'node:test';
import assert from 'node:assert';
import { modulePath } from './test_config.js';

// Force WASM; keeps the test independent of whether a native addon is present.
process.env.C4_DISABLE_NATIVE = '1';

const pkg = await import(modulePath);
const Colibri = pkg.default;

test('runtime.configureChain accepts a valid ethereum chain payload', async () => {
    const { getRuntime } = await import(modulePath.replace(/index\.js$/, 'runtime.js'));
    const runtime = await getRuntime();
    const err = runtime.configureChain(JSON.stringify({
        chain_type: 'ethereum',
        chain_id: 0xcafe01,
        genesis_validators_root: '0x0000000000000000000000000000000000000000000000000000000000000000',
        fork_epochs: [0],
        fork_versions: ['0x00000000', '0x01000000'],
    }));
    assert.strictEqual(err, null, `unexpected error: ${err}`);
});

test('runtime.configureChain surfaces structured errors', async () => {
    const { getRuntime } = await import(modulePath.replace(/index\.js$/, 'runtime.js'));
    const runtime = await getRuntime();
    // Missing chain_type -> wrapper rejects.
    const err = runtime.configureChain('{"chain_id":42}');
    assert.ok(err, 'expected an error string');
    assert.match(err, /chain_type/);
});

test('runtime.configureChain rejects unknown chain_type strings', async () => {
    const { getRuntime } = await import(modulePath.replace(/index\.js$/, 'runtime.js'));
    const runtime = await getRuntime();
    const err = runtime.configureChain('{"chain_type":"solana","chain_id":1}');
    assert.ok(err);
    assert.match(err, /unknown chain_type/);
});

test('C4Client lazily registers chain_configs before the first call', async () => {
    const client = new Colibri({
        chainId: 0xcafe02,
        rpcs: [],
        beacon_apis: [],
        chain_configs: [{
            chain_type: 'ethereum',
            chain_id: 0xcafe02,
            genesis_validators_root: '0x0000000000000000000000000000000000000000000000000000000000000000',
            fork_epochs: [0],
            fork_versions: ['0x00000000', '0x01000000'],
        }],
    });

    // getMethodSupport is the lightest entry point that triggers
    // ensure_chain_configured() and then hits the generated dispatcher.
    // For an unknown chain id with no verifier plugin we'd get UNDEFINED;
    // here the registration succeeded so eth_chainId (LOCAL) is recognised.
    const kind = await client.getMethodSupport('eth_chainId');
    assert.ok(typeof kind === 'number', `expected numeric MethodType, got ${typeof kind}`);
});

test('C4Client throws when a chain_configs entry is invalid', async () => {
    const client = new Colibri({
        chainId: 0xcafe03,
        rpcs: [],
        beacon_apis: [],
        chain_configs: [{
            // Missing all mandatory fields -> inner handler rejects.
            chain_type: 'ethereum',
            chain_id: 0xcafe03,
        }],
    });

    await assert.rejects(
        () => client.getMethodSupport('eth_chainId'),
        /c4_configure_chain failed/,
    );
});
