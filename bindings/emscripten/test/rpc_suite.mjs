// Shared RPC/proof fixture suite, executed against both the WASM runtime
// (rpc.test.mjs) and the native Node.js addon (native.test.mjs).

import assert from 'node:assert';
import * as fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const __dirname = dirname(fileURLToPath(import.meta.url));
export const testdir = join(__dirname, '../../../test/data');

function create_cache(dir) {
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
 * Registers a fixture-backed storage on the Colibri class and returns the
 * client config for the given fixture directory under test/data.
 * @param Colibri the Colibri client class
 * @param name fixture directory name (e.g. 'eth_getBalance1')
 * @return {test_conf, conf} the parsed test.json and the client config
 */
export async function setup_fixture(Colibri, name) {
    await Colibri.reset_caches();
    const dir = `${testdir}/${name}`;
    const cache = {}
    Colibri.register_storage({
        get: (key) => {
            try {
                let data = cache[key] ?? fs.readFileSync(`${dir}/${key}`);
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

    const test_conf = JSON.parse(fs.readFileSync(`${dir}/test.json`, 'utf8'));
    // The fixtures under test/data are static recordings whose `latest`
    // blocks are inevitably stale, so disable the freshness check here
    // (otherwise `eth_call`/simulate proofs fail with "proof for latest
    // too old"). The check itself is covered by test_verify_call_freshness.
    const conf = { chainId: test_conf.chain_id, cache: create_cache(dir), max_latest_age_seconds: 0 }
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
    return { test_conf, conf };
}

/**
 * Registers all fixture tests from test/data on the given node:test context.
 * @param t node:test context
 * @param Colibri the Colibri client class (default export of the module under test)
 * @param decode_proof optional decode_proof function; when given, each created proof is also decoded
 */
export async function run_rpc_suite(t, Colibri, decode_proof) {
    await t.test('should load module', async () => {
        const c4 = new Colibri();
        const result = await c4.getMethodSupport('eth_getTransactionByHash');
        assert.strictEqual(result, 1 /*MethodType.PROOFABLE*/, 'Method should be proofable');
    });

    if (decode_proof)
        await t.test('decode_proof rejects unknown formats', async () => {
            // first byte 0xff selects an unknown chain type -> no request container
            await assert.rejects(() => decode_proof(new Uint8Array([0xff, 0xff, 0xff, 0xff, 0xff])), /Unknown proof format/);
        });

    const tests = fs.readdirSync(testdir).filter(f => fs.existsSync(`${testdir}/${f}/test.json`));
    for (const test of tests) {
        await t.test(`run ${test}`, async () => {
            const { test_conf, conf } = await setup_fixture(Colibri, test);
            if (test_conf.requires_chain_store) return;

            const c4 = new Colibri(conf);

            if (conf.privacy_mode == "basic") {
                const result = await c4.rpc(test_conf.method, test_conf.params);
                assert.deepStrictEqual(result, test_conf.expected_result, 'Proof should be valid');
                return;
            }

            // benchmark createProof
            const createProofStart = performance.now();
            const proof = await c4.createProof(test_conf.method, test_conf.params)
            const createProofEnd = performance.now();
            const createProofDuration = createProofEnd - createProofStart;

            assert.strictEqual(proof.length > 0, true, 'Proof should be non-empty');

            if (decode_proof) {
                const decoded = await decode_proof(proof);
                assert.ok(decoded && typeof decoded === 'object', 'decoded proof should be an object');
            }

            // benchmark verifyProof
            const verifyProofStart = performance.now();
            const result = await c4.verifyProof(test_conf.method, test_conf.params, proof);
            const verifyProofEnd = performance.now();
            const verifyProofDuration = verifyProofEnd - verifyProofStart;

            const totalDuration = createProofDuration + verifyProofDuration;

            if (process.env.BENCHMARK) {
                console.log(`\n=== Benchmark for ${test} ===`);
                console.log(`createProof: ${createProofDuration.toFixed(2)}ms`);
                console.log(`verifyProof: ${verifyProofDuration.toFixed(2)}ms`);
                console.log(`Total: ${totalDuration.toFixed(2)}ms`);
                console.log(`================================\n`);
            }
            assert.deepEqual(result, test_conf.expected_result, 'Proof should be valid');
        });
    }
}
