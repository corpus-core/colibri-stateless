import test from 'node:test';
import assert from 'node:assert';
import { createRequire } from 'node:module';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { modulePath } from './test_config.js';

test('CommonJS bridge can load package', async () => {
  const here = dirname(fileURLToPath(import.meta.url));
  const builtIndexJs = resolve(here, modulePath);
  const pkgRoot = dirname(builtIndexJs);

  // Use require() even though this test is ESM.
  const require = createRequire(import.meta.url);
  const cjs = require(pkgRoot);

  assert.strictEqual(typeof cjs.default, 'function', 'CJS entry should provide default export (Colibri class)');
  assert.strictEqual(typeof cjs.Strategy, 'object', 'CJS entry should provide Strategy export');
  assert.strictEqual(typeof cjs.set_wasm_url, 'function', 'CJS entry should provide set_wasm_url export');
});
