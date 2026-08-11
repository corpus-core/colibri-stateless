// Shared RPC/proof fixture suite, executed against both the WASM runtime
// (rpc.test.mjs) and the native Node.js addon (native.test.mjs).

import assert from 'node:assert';
import * as fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const __dirname = dirname(fileURLToPath(import.meta.url));
export const testdir = join(__dirname, '../../../test/data');

export function create_cache(dir) {
    return {
        cacheable(req) {
            return true;
        },
        get(req) {
            let name = ''
            if (req.url) {
                // Mirror `c4_req_mockname` in src/util/state.c: cache-friendly proof URLs of the
                // form `proof/<method>/<block>/<version>/<zk|std>/<c4>` are compressed to
                // `proof/<method>/<block>` so fixtures survive client-version bumps, zk toggles
                // and client-state changes.
                if (req.url.startsWith('proof/')) {
                    const rest = req.url.slice(6)
                    const firstSlash = rest.indexOf('/')
                    const secondSlash = firstSlash >= 0 ? rest.indexOf('/', firstSlash + 1) : -1
                    if (firstSlash >= 0 && secondSlash >= 0)
                        name = 'proof/' + rest.slice(0, secondSlash)
                    else
                        name = req.url
                } else {
                    name = req.url
                }
            }
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


            //            console.log(`::: ${dir}/${name}`)

            if (fs.existsSync(`${dir}/${name}`))
                return fs.readFileSync(`${dir}/${name}`);
            throw new Error(`Testdata not found for: ${dir}/${name} for ${JSON.stringify(req, null, 2)}`)
        },
        set(req, data) {
        }
    }
}

/**
 * Registers all fixture tests from test/data on the given node:test context.
 * @param t node:test context
 * @param Colibri the Colibri client class (default export of the module under test)
 */
export async function run_rpc_suite(t, Colibri) {
    await t.test('should load module', async () => {
        const c4 = new Colibri();
        const result = await c4.getMethodSupport('eth_getTransactionByHash');
        assert.strictEqual(result, 1 /*MethodType.PROOFABLE*/, 'Method should be proofable');
    });

    const tests = fs.readdirSync(testdir).filter(f => fs.existsSync(`${testdir}/${f}/test.json`));
    for (const test of tests) {
        await t.test(`run ${test}`, async () => {
            const cache = {}
            Colibri.register_storage({
                get: (key) => {
                    try {
                        let data = cache[key] ?? fs.readFileSync(`${testdir}/${test}/${key}`);
                        if (data && key.startsWith('tx_pending_')) {
                            data = Buffer.from(data);
                            const now = BigInt(Math.floor(Date.now() / 1000));
                            for (let i = 0; i + 40 <= data.length; i += 40)
                                data.writeBigUInt64LE(now, i + 32);
                        }
                        return data;
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
            if (test_conf.requires_chain_store) return;
            // The fixtures under test/data are static recordings whose `latest`
            // blocks are inevitably stale, so disable the freshness check here
            // (otherwise `eth_call`/simulate proofs fail with "proof for latest
            // too old"). The check itself is covered by test_verify_call_freshness.
            let conf = { chainId: test_conf.chain_id, cache: create_cache(`${testdir}/${test}`), max_latest_age_seconds: 0 }
            if (test_conf.trusted_blockhash)
                conf.trusted_checkpoint = test_conf.trusted_blockhash
            if (test_conf.include_code)
                conf.include_code = true
            if ('use_accesslist' in test_conf)
                conf.use_accesslist = test_conf.use_accesslist
            if (test_conf.pap)
                conf.privacy_mode = "basic";
            if (test_conf.remote_prover)
                conf.prover = ["http://mock-prover"];
            else
                conf.prover = [];

            const c4 = new Colibri(conf);

            if (conf.privacy_mode == "basic") {
                const result = await c4.rpc(test_conf.method, test_conf.params);
                assert.deepStrictEqual(result, test_conf.expected_result, 'Proof should be valid');
                return;
            }

            // Benchmark für createProof
            const createProofStart = performance.now();
            const proof = await c4.createProof(test_conf.method, test_conf.params)
            const createProofEnd = performance.now();
            const createProofDuration = createProofEnd - createProofStart;

            assert.strictEqual(proof.length > 0, true, 'Proof should be non-empty');

            // Benchmark für verifyProof
            const verifyProofStart = performance.now();
            const result = await c4.verifyProof(test_conf.method, test_conf.params, proof);
            const verifyProofEnd = performance.now();
            const verifyProofDuration = verifyProofEnd - verifyProofStart;

            const totalDuration = createProofDuration + verifyProofDuration;

            if (process.env.BENCHMARK) {

                // Ausgabe der Benchmark-Ergebnisse
                console.log(`\n=== Benchmark für ${test} ===`);
                console.log(`createProof: ${createProofDuration.toFixed(2)}ms`);
                console.log(`verifyProof: ${verifyProofDuration.toFixed(2)}ms`);
                console.log(`Gesamt: ${totalDuration.toFixed(2)}ms`);
                console.log(`================================\n`);

            }
            assert.deepEqual(result, test_conf.expected_result, 'Proof should be valid');
        });
    }
}
