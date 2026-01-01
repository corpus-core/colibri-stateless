import test from 'node:test';
import assert from 'node:assert';
import * as fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { modulePath } from './test_config.js';

// Force the Node.js runtime to use the native addon and fail if it cannot be loaded.
process.env.C4W_FORCE_NATIVE = '1';

const nodeModulePath = modulePath.replace(/index\.js$/, 'index.node.js');
const ColibriModule = await import(nodeModulePath);
const Colibri = ColibriModule.default;

const __dirname = dirname(fileURLToPath(import.meta.url));
const testdir = join(__dirname, '../../../test/data');

function create_cache(dir) {
  return {
    cacheable() {
      return true;
    },
    get(req) {
      let name = '';
      if (req.url) name = req.url;
      else if (req.payload)
        name = req.payload.method + req.payload.params.map(p => '_' + ((typeof p == 'string' ? p : JSON.stringify(p)))).join('');

      for (let i = 0; i < name.length; i++) {
        switch (name[i]) {
          case '/':
          case '.':
          case ',':
          case ' ':
          case ':':
          case '=':
          case '?':
          case '"':
          case '&':
          case '[':
          case ']':
          case '{':
          case '}':
            name = name.slice(0, i) + '_' + name.slice(i + 1);
            break;
          default:
            break;
        }
      }

      if (name.length > 100) name = name.slice(0, 100);
      name = name + '.' + req.encoding;

      if (fs.existsSync(`${dir}/${name}`))
        return fs.readFileSync(`${dir}/${name}`);
      throw new Error(`Testdata not found for: ${dir}/${name} for ${JSON.stringify(req, null, 2)}`);
    },
    set() {
    }
  };
}

test('Native addon smoke tests', async (t) => {
  await t.test('should load Node entry and require native addon', async () => {
    const c4 = new Colibri();
    const result = await c4.getMethodSupport('eth_getTransactionByHash');
    assert.strictEqual(result, 1 /*MethodType.PROOFABLE*/);
  });

  // Run a small subset to keep CI runtime bounded (WASM path is already fully covered).
  const selected = new Set(['eth_getBalance1', 'eth_call1', 'trusted_block1']);
  const tests = fs.readdirSync(testdir).filter(f => selected.has(f) && fs.existsSync(`${testdir}/${f}/test.json`));

  for (const name of tests) {
    await t.test(`run ${name} (native)`, async () => {
      const cache = {};
      Colibri.register_storage({
        get: (key) => {
          try {
            return cache[key] ?? fs.readFileSync(`${testdir}/${name}/${key}`);
          } catch {
            return null;
          }
        },
        set: async (key, value) => {
          cache[key] = value;
        },
        del: (key) => {
          delete cache[key];
        }
      });

      const test_conf = JSON.parse(fs.readFileSync(`${testdir}/${name}/test.json`, 'utf8'));
      if (test_conf.requires_chain_store) return;
      const conf = { chain: test_conf.chain, cache: create_cache(`${testdir}/${name}`) };
      if (test_conf.trusted_blockhash) conf.trusted_checkpoint = test_conf.trusted_blockhash;

      const c4 = new Colibri(conf);
      const proof = await c4.createProof(test_conf.method, test_conf.params);
      assert.strictEqual(proof.length > 0, true);
      const result = await c4.verifyProof(test_conf.method, test_conf.params, proof);
      assert.deepEqual(result, test_conf.expected_result);
    });
  }
});


