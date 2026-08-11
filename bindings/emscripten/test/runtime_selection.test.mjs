// Tests the runtime selection logic of the Node entry point (index.node.js):
// native addon vs. WASM fallback, controlled via environment variables.
//
// Each case runs in a child process because the runtime is cached per process
// and the env vars are read during initialization.

import test from 'node:test';
import assert from 'node:assert';
import * as fs from 'node:fs';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { modulePath } from './test_config.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const nodeEntry = join(__dirname, modulePath.replace(/index\.js$/, 'index.node.js'));
const cjsEntry = join(dirname(nodeEntry), 'cjs', 'index.node.js');
const localAddon = join(__dirname, '../../../build/node-addon/node-addon/colibri_native.node');

// When a packaged prebuild exists (CI), the loader finds the addon even
// without C4_NATIVE_ADDON, so the not-found scenarios cannot be simulated.
const target = `${process.platform}-${process.arch}`;
const prebuildExists =
    fs.existsSync(join(dirname(nodeEntry), 'prebuilds', target, 'colibri_native.node')) ||
    fs.existsSync(join(dirname(dirname(nodeEntry)), 'prebuilds', target, 'colibri_native.node'));

function getRuntimeKind(env, entry = nodeEntry, cjs = false) {
    const script = cjs
        ? `require(${JSON.stringify(entry)}).getRuntime().then(rt => console.log('KIND=' + rt.kind));`
        : `import(${JSON.stringify(entry)}).then(m => m.getRuntime()).then(rt => console.log('KIND=' + rt.kind));`;
    return spawnSync(process.execPath, ['-e', script], {
        encoding: 'utf8',
        env: { ...process.env, C4_DISABLE_NATIVE: '', C4_FORCE_NATIVE: '', C4_NATIVE_ADDON: '', ...env },
    });
}

test('C4_DISABLE_NATIVE=1 always selects the WASM runtime', () => {
    const res = getRuntimeKind({ C4_DISABLE_NATIVE: '1' });
    assert.strictEqual(res.status, 0, res.stderr);
    assert.match(res.stdout, /KIND=wasm/);
});

test('missing addon silently falls back to WASM', { skip: prebuildExists && 'prebuild present, not-found path cannot be simulated' }, () => {
    const res = getRuntimeKind({ C4_NATIVE_ADDON: '/nonexistent/colibri_native.node' });
    assert.strictEqual(res.status, 0, res.stderr);
    assert.match(res.stdout, /KIND=wasm/);
});

test('C4_FORCE_NATIVE=1 fails instead of falling back', { skip: prebuildExists && 'prebuild present, not-found path cannot be simulated' }, () => {
    const res = getRuntimeKind({ C4_NATIVE_ADDON: '/nonexistent/colibri_native.node', C4_FORCE_NATIVE: '1' });
    assert.notStrictEqual(res.status, 0, 'process should fail');
    assert.match(res.stderr, /no native colibri addon found/);
});

test('C4_NATIVE_ADDON selects the native runtime (ESM)', { skip: !fs.existsSync(localAddon) && 'no local addon build' }, () => {
    const res = getRuntimeKind({ C4_NATIVE_ADDON: localAddon });
    assert.strictEqual(res.status, 0, res.stderr);
    assert.match(res.stdout, /KIND=native/);
});

test('C4_NATIVE_ADDON selects the native runtime (CJS)', { skip: (!fs.existsSync(localAddon) || !fs.existsSync(cjsEntry)) && 'no local addon or cjs build' }, () => {
    const res = getRuntimeKind({ C4_NATIVE_ADDON: localAddon }, cjsEntry, true);
    assert.strictEqual(res.status, 0, res.stderr);
    assert.match(res.stdout, /KIND=native/);
});
