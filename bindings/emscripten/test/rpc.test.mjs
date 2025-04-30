// Create bindings/emscripten/test/basic.test.ts

import test from 'node:test';
import assert from 'node:assert';
import * as fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import path from 'path';
import { modulePath } from './test_config.js';

// Dynamically import the module using the path from test_config.js
const ColibriModule = await import(modulePath);
const Colibri = ColibriModule.default; // Assuming Colibri is the default export
const MethodType = ColibriModule.MethodType; // Assuming MethodType is a named export

const __dirname = dirname(fileURLToPath(import.meta.url));
const testdir = join(__dirname, '../../../test/data');

function create_cache(dir) {
    return {
        cacheable(req) {
            return true;
        },
        get(req) {
            let name = ''
            if (req.url) name = req.url
            else if (req.payload)
                name = req.payload.method + req.payload.params.map(p => '_' + ((typeof p == 'string' ? p : JSON.stringify(p)))).join('')


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
                        name = name.slice(0, i) + '_' + name.slice(i + 1)
                        break;
                    default:
                        break;
                }
            }

            if (name.length > 100) name = name.slice(0, 100)
            name = name + '.' + req.encoding

            return fs.readFileSync(`${dir}/${name}`);
        },
        set(req, data) {
        }
    }
}


test('RPC-Proof Test Suite', async (t) => {
    await t.test('should load Emscripten module', async () => {
        const c4 = new Colibri();
        const result = await c4.getMethodSupport('eth_getTransactionByHash');
        assert.strictEqual(result, MethodType.PROOFABLE, 'Method should be proofable');
        console.log(result);
    });


    const tests = fs.readdirSync(testdir).filter(f => fs.existsSync(`${testdir}/${f}/test.json`));
    for (const test of tests) {
        await t.test(`run ${test}`, async () => {
            const cache = {}
            Colibri.register_storage({
                get: (key) => {
                    try {
                        return cache[key] ?? fs.readFileSync(`${testdir}/${test}/${key}`);
                    } catch (e) {
                        return null;
                    }
                },
                set: async (key, value) => {
                    cache[key] = value;
                },
                del: (key) => {
                    delete cache[key];
                }
            })

            let test_conf = JSON.parse(fs.readFileSync(`${testdir}/${test}/test.json`, 'utf8'));
            let conf = { chain: test_conf.chain, cache: create_cache(`${testdir}/${test}`) }
            if (test_conf.trusted_blockhash)
                conf.trusted_block_hashes = [test_conf.trusted_blockhash]
            const c4 = new Colibri(conf);
            const proof = await c4.createProof(test_conf.method, test_conf.params)
            assert.strictEqual(proof.length > 0, true, 'Proof should be non-empty');
            const result = await c4.verifyProof(test_conf.method, test_conf.params, proof);
            assert.deepEqual(result, test_conf.expected_result, 'Proof should be valid');
        });
    }
});