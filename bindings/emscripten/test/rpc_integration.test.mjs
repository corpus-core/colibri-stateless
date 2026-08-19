/**
 * Live RPC integration test for the WASM/Node Colibri client.
 *
 * Unlike `integration.test.mjs` (first-sync / checkpoint bootstrap), this suite
 * exercises real RPC methods across prover modes and privacy settings and
 * compares every Colibri result against the same request sent to an execution
 * JSON-RPC endpoint. Array order may differ; values must match.
 *
 * Run:
 *   C4_RUN_RPC_INTEGRATION=1 npm run test:rpc-integration
 *   ./test/run_rpc_integration_local.sh   # prover = http://localhost:8090
 *
 * Optional env / flags: C4_RPC_URL, C4_BEACON_URL, C4_PROVER_URL, C4_CHAIN_ID,
 * C4_MODES, C4_PRIVACY, C4_COMPARE, --rpc, --beacon, --prover, --modes,
 * --privacy, --compare, --debug
 *
 * `--compare` / `C4_COMPARE` (default: extra):
 *   extra   overlapping keys must match; extra keys on either side are allowed
 *   values  extra RPC keys allowed; extra non-empty Colibri keys fail (except
 *           documented OP-Stack optional fields)
 *   strict  both objects must have the same keys; extra keys on either side fail
 */

import test, { describe, before } from 'node:test';
import assert from 'node:assert';
import { modulePath } from './test_config.js';

const ColibriModule = await import(modulePath);
const Colibri = ColibriModule.default;

const RUN = process.env.C4_RUN_RPC_INTEGRATION === '1';
const METHOD_TIMEOUT = 180_000;
const REQUEST_COUNT = 14;
const RUN_TIMEOUT = METHOD_TIMEOUT * (REQUEST_COUNT + 2);
const PARENT_TIMEOUT = RUN_TIMEOUT * 2 + 5 * 60_000;

/** Mainnet fixtures. Other chain IDs need matching addresses via a follow-up. */
const USDC = '0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48';
const USDT = '0xdac17f958d2ee523a2206206994597c13d831ec7';
const BALANCE_OF = '0x70a0823100000000000000000000000037305b1cd40574e4c5ce33f8e8306be057fd7341';
const FEE_RECIPIENT = '0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5';

const DEFAULT_RPC_BY_CHAIN = {
  1: 'https://mainnet.colibri-proof.tech/execution',
  11155111: 'https://sepolia.colibri-proof.tech/execution',
  100: 'https://gnosis.colibri-proof.tech/execution',
};

/** SSZ optional fields that JSON-RPC nodes typically omit on mainnet. */
const COLIBRI_ONLY_OPTIONAL = new Set([
  'sourceHash',
  'mint',
  'isSystemTx',
  'depositReceiptVersion',
  'depositNonce',
]);

const LIVE_TAGS = new Set(['latest', 'safe', 'finalized']);
/** Execution RPC and Colibri can disagree on the live head by a slot or two. */
const LIVE_HEAD_TOLERANCE = 2;

// ---------------------------------------------------------------------------
// CLI / env
// ---------------------------------------------------------------------------

function argVal(name, envName, fallback) {
  const argv = process.argv.slice(2);
  const idx = argv.indexOf('--' + name);
  if (idx >= 0 && argv[idx + 1] && !argv[idx + 1].startsWith('--')) return argv[idx + 1];
  if (process.env[envName]) return process.env[envName];
  return fallback;
}

function splitList(value) {
  if (!value) return null;
  const list = value.split(',').map((s) => s.trim()).filter(Boolean);
  return list.length ? list : null;
}

const CHAIN_ID = parseInt(argVal('chain', 'C4_CHAIN_ID', '1'), 10);
const RPC_URLS = splitList(argVal('rpc', 'C4_RPC_URL', ''));
const BEACON_URLS = splitList(argVal('beacon', 'C4_BEACON_URL', ''));
const PROVER_URLS = splitList(argVal('prover', 'C4_PROVER_URL', ''));
const MODES = (argVal('modes', 'C4_MODES', 'local,remote,hybrid')).split(',').map((s) => s.trim()).filter(Boolean);
const PRIVACY = (argVal('privacy', 'C4_PRIVACY', 'none,basic')).split(',').map((s) => s.trim()).filter(Boolean);
const COMPARE_MODE = argVal('compare', 'C4_COMPARE', 'extra');
const DEBUG = process.argv.includes('--debug') || process.env.C4_DEBUG === '1';

if (!['extra', 'values', 'strict'].includes(COMPARE_MODE)) {
  throw new Error(`Invalid --compare / C4_COMPARE="${COMPARE_MODE}" (expected extra|values|strict)`);
}

const GROUND_TRUTH_RPC = (RPC_URLS && RPC_URLS[0]) || DEFAULT_RPC_BY_CHAIN[CHAIN_ID] || null;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function createMemoryStorage() {
  const map = new Map();
  return {
    get: (key) => map.get(key) ?? null,
    set: (key, value) => map.set(key, new Uint8Array(value)),
    del: (key) => map.delete(key),
    _map: map,
  };
}

function cacheKey(req) {
  return JSON.stringify({
    type: req.type,
    url: req.url,
    method: req.method,
    encoding: req.encoding,
    payload: req.payload,
  });
}

function createMemoryCache() {
  const map = new Map();
  const cache = {
    hits: 0,
    cacheable: () => true,
    get: (req) => {
      const v = map.get(cacheKey(req)) ?? null;
      if (v) cache.hits++;
      return v;
    },
    set: (req, data) => map.set(cacheKey(req), data),
    clear: () => {
      map.clear();
      cache.hits = 0;
    },
    get size() {
      return map.size;
    },
  };
  return cache;
}

async function rpcFetch(method, params) {
  const res = await fetch(GROUND_TRUTH_RPC, {
    method: 'POST',
    body: JSON.stringify({ jsonrpc: '2.0', id: 1, method, params }),
    headers: { 'Content-Type': 'application/json' },
  });
  if (!res.ok) {
    const body = await res.text().catch(() => '');
    throw new Error(`RPC HTTP ${res.status} ${res.statusText}${body ? `: ${body.slice(0, 200)}` : ''}`);
  }
  const json = await res.json();
  if (json.error) throw new Error(json.error.message || JSON.stringify(json.error));
  return json.result;
}

async function rpcFetchWithRetry(method, params, retries = 3) {
  for (let i = 0; i < retries; i++) {
    try {
      return await rpcFetch(method, params);
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      if (i === retries - 1) throw e;
      if (msg.includes('429') || msg.includes('rate') || msg.includes('timeout') || msg.includes('fetch')) {
        await sleep(2000 * (i + 1));
        continue;
      }
      await sleep(1000 * (i + 1));
    }
  }
  throw new Error('Unreachable');
}

function toHexBlock(n) {
  return '0x' + n.toString(16);
}

function txHashFromBlock(block) {
  const tx = block?.transactions?.[0];
  if (!tx) return null;
  return typeof tx === 'string' ? tx : tx.hash;
}

function createClient({ proverMode, privacyMode, cache, logsCompleteness }) {
  const conf = {
    chainId: CHAIN_ID,
    prover_mode: proverMode,
    privacy_mode: privacyMode,
    zk_proof: true,
    cache,
    debug: DEBUG,
    // Warm replay of a cached "latest" proof must not die on the 60s freshness window.
    max_latest_age_seconds: 0,
  };
  if (logsCompleteness) conf.logs_completeness = true;
  if (RPC_URLS) conf.rpcs = RPC_URLS;
  if (BEACON_URLS) conf.beacon_apis = BEACON_URLS;
  if (proverMode === 'local') conf.prover = [];
  else if (PROVER_URLS) conf.prover = PROVER_URLS;
  return new Colibri(conf);
}

function comboLabel(mode, privacy) {
  return `${mode} + privacy=${privacy}`;
}

function isLiveRequest(req) {
  if (req.live) return true;
  return req.params.some((p) => typeof p === 'string' && LIVE_TAGS.has(p));
}

function parseHexQuantity(v) {
  if (typeof v === 'number' && Number.isFinite(v)) return v;
  if (typeof v === 'bigint') return Number(v);
  if (typeof v === 'string' && /^0x[0-9a-fA-F]+$/i.test(v)) return parseInt(v, 16);
  return NaN;
}

function blockDelta(a, b) {
  const na = parseHexQuantity(a);
  const nb = parseHexQuantity(b);
  if (!Number.isFinite(na) || !Number.isFinite(nb)) return Infinity;
  return Math.abs(na - nb);
}

// ---------------------------------------------------------------------------
// Value comparison (order-insensitive, RPC is the source of truth)
// ---------------------------------------------------------------------------

function isEmptyValue(v) {
  if (v === null || v === undefined) return true;
  if (Array.isArray(v) && v.length === 0) return true;
  return false;
}

function canonScalar(v) {
  if (typeof v === 'bigint') return '0x' + v.toString(16);
  if (typeof v === 'number' && Number.isFinite(v) && Number.isInteger(v)) {
    return '0x' + BigInt(v).toString(16);
  }
  if (typeof v === 'string' && /^0x[0-9a-fA-F]*$/i.test(v)) {
    const hex = v.slice(2).toLowerCase();
    if (hex.length === 40 || hex.length === 64 || hex.length === 512) return '0x' + hex;
    if (hex.length > 16 && hex.length % 2 === 0) return '0x' + hex;
    return '0x' + (hex.replace(/^0+/, '') || '0');
  }
  return v;
}

function normalize(v) {
  if (v === null || v === undefined) return null;
  if (Array.isArray(v)) return v.map(normalize);
  if (typeof v === 'object') {
    const out = {};
    for (const k of Object.keys(v)) {
      if (k.startsWith('_')) continue;
      const n = normalize(v[k]);
      if (n === null) continue;
      out[k] = n;
    }
    return out;
  }
  return canonScalar(v);
}

function fmtVal(v) {
  const s = typeof v === 'string' ? v : JSON.stringify(v);
  return s.length > 240 ? s.slice(0, 240) + '…' : s;
}

/**
 * Compare Colibri against RPC. Array order is ignored. Shared values must match.
 * Key-set strictness is controlled by `COMPARE_MODE`.
 * @param rpc RPC JSON-RPC result
 * @param colibri Colibri verified result
 * @param path JSON path for error messages
 */
function assertSameValues(rpc, colibri, path) {
  const a = normalize(rpc);
  const b = normalize(colibri);
  assertSameNormalized(a, b, path);
}

function allowExtraColibriKey(key, value) {
  if (COMPARE_MODE === 'extra') return true;
  if (COMPARE_MODE === 'strict') return false;
  return COLIBRI_ONLY_OPTIONAL.has(key) || isEmptyValue(value);
}

function identityOf(obj) {
  if (!obj || typeof obj !== 'object' || Array.isArray(obj)) return null;
  if (typeof obj.transactionHash === 'string' && obj.data !== undefined) {
    return `log:${obj.transactionHash}:${JSON.stringify(obj.topics || [])}:${obj.data}`;
  }
  if (typeof obj.transactionHash === 'string') return `receipt:${obj.transactionHash}`;
  if (typeof obj.hash === 'string' && obj.parentHash) return `block:${obj.hash}`;
  if (typeof obj.hash === 'string') return `tx:${obj.hash}`;
  return null;
}

function assertSameNormalized(rpc, colibri, path) {
  if (rpc === colibri) return;

  if (isEmptyValue(colibri) && isEmptyValue(rpc)) return;

  if (Array.isArray(rpc) && Array.isArray(colibri)) {
    if (rpc.length !== colibri.length) {
      assert.fail(`${path}: array length RPC=${rpc.length} Colibri=${colibri.length}`);
    }
    // Match as a multiset. Prefer identity fields so a thin Colibri object cannot
    // greedily pair with an unrelated RPC element that shares one field.
    const used = new Array(rpc.length).fill(false);
    for (let i = 0; i < colibri.length; i++) {
      const id = identityOf(colibri[i]);
      let matched = false;
      let lastErr = null;
      const tryIdx = [];
      if (id) {
        for (let j = 0; j < rpc.length; j++) {
          if (!used[j] && identityOf(rpc[j]) === id) tryIdx.push(j);
        }
      }
      for (let j = 0; j < rpc.length; j++) {
        if (!used[j] && !tryIdx.includes(j)) tryIdx.push(j);
      }
      for (const j of tryIdx) {
        try {
          assertSameNormalized(rpc[j], colibri[i], `${path}[${i}]`);
          used[j] = true;
          matched = true;
          break;
        } catch (e) {
          lastErr = e;
        }
      }
      if (!matched) {
        const hint = lastErr instanceof Error ? ` (last: ${lastErr.message})` : '';
        assert.fail(`${path}[${i}]: no matching RPC element for Colibri value ${fmtVal(colibri[i])}${hint}`);
      }
    }
    return;
  }

  const rpcObj = rpc !== null && typeof rpc === 'object' && !Array.isArray(rpc);
  const colObj = colibri !== null && typeof colibri === 'object' && !Array.isArray(colibri);
  if (rpcObj && colObj) {
    if (Object.keys(colibri).length === 0 && Object.keys(rpc).length > 0) {
      assert.fail(`${path}: Colibri object is empty but RPC has keys ${Object.keys(rpc).join(', ')}`);
    }
    for (const k of Object.keys(colibri)) {
      if (!(k in rpc)) {
        if (allowExtraColibriKey(k, colibri[k])) continue;
        assert.fail(`${path}.${k}: Colibri has extra value ${fmtVal(colibri[k])} not present in RPC`);
      }
      assertSameNormalized(rpc[k], colibri[k], `${path}.${k}`);
    }
    if (COMPARE_MODE === 'strict') {
      for (const k of Object.keys(rpc)) {
        if (k in colibri) continue;
        assert.fail(`${path}.${k}: RPC has extra value ${fmtVal(rpc[k])} not present in Colibri`);
      }
    }
    return;
  }

  assert.fail(`${path}: RPC=${fmtVal(rpc)} Colibri=${fmtVal(colibri)}`);
}

async function assertLiveHeadQuantity(label, rpcVal, colibriVal) {
  let delta = blockDelta(rpcVal, colibriVal);
  if (delta <= LIVE_HEAD_TOLERANCE) return;
  const retry = await rpcFetchWithRetry('eth_blockNumber', []);
  delta = blockDelta(retry, colibriVal);
  if (delta <= LIVE_HEAD_TOLERANCE) return;
  assert.fail(
    `${label}: live head delta ${delta} > ${LIVE_HEAD_TOLERANCE} (RPC=${fmtVal(retry)} Colibri=${fmtVal(colibriVal)})`,
  );
}

async function compareLiveBlock(req, colibriVal, rpcVal) {
  try {
    assertSameValues(rpcVal, colibriVal, req.label);
    return rpcVal;
  } catch {
    const includeTxs = req.params[1];
    const rpcNum = rpcVal && typeof rpcVal === 'object' ? rpcVal.number : undefined;
    const c4Num = colibriVal && typeof colibriVal === 'object' ? colibriVal.number : undefined;
    let delta = blockDelta(rpcNum, c4Num);
    if (delta > LIVE_HEAD_TOLERANCE) {
      const retry = await rpcFetchWithRetry(req.method, req.params);
      try {
        assertSameValues(retry, colibriVal, req.label + ' (rpc retry)');
        return retry;
      } catch {
        delta = blockDelta(retry && retry.number, c4Num);
        if (delta > LIVE_HEAD_TOLERANCE) {
          assert.fail(
            `${req.label}: live head delta ${delta} > ${LIVE_HEAD_TOLERANCE} ` +
            `(RPC=${fmtVal(retry?.number)} Colibri=${fmtVal(c4Num)})`,
          );
        }
      }
    }
    if (c4Num === undefined) throw new Error(`${req.label}: Colibri block has no number`);
    // Same values, just a slightly older/newer live tag: pin to the block Colibri proved.
    const pinned = await rpcFetchWithRetry('eth_getBlockByNumber', [c4Num, includeTxs]);
    assertSameValues(pinned, colibriVal, req.label + ' (pinned to Colibri head)');
    return colibriVal;
  }
}

async function compareAgainstRpc(req, colibriVal, rpcVal) {
  if (req.method === 'eth_blockNumber') {
    await assertLiveHeadQuantity(req.label, rpcVal, colibriVal);
    return colibriVal;
  }
  if (req.method === 'eth_getBlockByNumber' && isLiveRequest(req)) {
    return compareLiveBlock(req, colibriVal, rpcVal);
  }
  try {
    assertSameValues(rpcVal, colibriVal, req.label);
    return rpcVal;
  } catch (err) {
    if (!isLiveRequest(req)) throw err;
    const retry = await rpcFetchWithRetry(req.method, req.params);
    assertSameValues(retry, colibriVal, req.label + ' (rpc retry)');
    return retry;
  }
}

// ---------------------------------------------------------------------------
// Test data + request set
// ---------------------------------------------------------------------------

async function resolveTestData() {
  console.log('Resolving dynamic test data...');
  console.log(`  Ground-truth RPC: ${GROUND_TRUTH_RPC}`);
  console.log(`  Chain: ${CHAIN_ID}`);
  if (RPC_URLS) console.log(`  Colibri rpcs: ${RPC_URLS.join(', ')}`);
  if (BEACON_URLS) console.log(`  Colibri beacon: ${BEACON_URLS.join(', ')}`);
  if (PROVER_URLS) console.log(`  Colibri prover: ${PROVER_URLS.join(', ')}`);
  else console.log('  Colibri prover: (client default / empty for local)');
  console.log(`  Modes: ${MODES.join(', ')}`);
  console.log(`  Privacy: ${PRIVACY.join(', ')}`);
  console.log(`  Compare: ${COMPARE_MODE}`);

  const blockNumHex = await rpcFetchWithRetry('eth_blockNumber', []);
  const currentBlock = parseInt(blockNumHex, 16) - 2;
  console.log(`  Head-2: ${currentBlock} (${toHexBlock(currentBlock)})`);

  let pinnedBlock = null;
  let txHash = null;
  for (let i = 0; i < 20; i++) {
    const hex = toHexBlock(currentBlock - i);
    const block = await rpcFetchWithRetry('eth_getBlockByNumber', [hex, true]);
    const hash = txHashFromBlock(block);
    if (hash) {
      pinnedBlock = hex;
      txHash = hash;
      break;
    }
  }
  if (!pinnedBlock || !txHash) throw new Error('No transactions in the last 20 blocks');
  console.log(`  Pinned block: ${pinnedBlock}`);
  console.log(`  TX: ${txHash}`);

  let logsBlock = pinnedBlock;
  for (let i = 0; i < 30; i++) {
    const hex = toHexBlock(currentBlock - i);
    const logs = await rpcFetchWithRetry('eth_getLogs', [{
      address: [USDT],
      fromBlock: hex,
      toBlock: hex,
    }]);
    if (Array.isArray(logs) && logs.length > 0) {
      logsBlock = hex;
      console.log(`  Logs block: ${hex} (${logs.length} USDT logs)`);
      break;
    }
  }
  if (logsBlock === pinnedBlock) console.log(`  Logs block: ${logsBlock} (may be empty)`);

  return { pinnedBlock, txHash, logsBlock };
}

function buildRequests(testData) {
  const callTx = { to: USDC, data: BALANCE_OF };
  const logsFilter = {
    address: [USDT],
    fromBlock: testData.logsBlock,
    toBlock: testData.logsBlock,
  };
  return [
    { label: 'eth_chainId', method: 'eth_chainId', params: [] },
    { label: 'eth_blockNumber', method: 'eth_blockNumber', params: [], live: true },
    { label: 'eth_getBlockByNumber(latest)', method: 'eth_getBlockByNumber', params: ['latest', false] },
    { label: 'eth_getBlockByNumber(safe)', method: 'eth_getBlockByNumber', params: ['safe', false] },
    { label: 'eth_getBlockByNumber(finalized)', method: 'eth_getBlockByNumber', params: ['finalized', false] },
    { label: 'eth_getBalance', method: 'eth_getBalance', params: [FEE_RECIPIENT, testData.pinnedBlock] },
    { label: 'eth_getCode', method: 'eth_getCode', params: [USDC, testData.pinnedBlock] },
    { label: 'eth_call', method: 'eth_call', params: [callTx, testData.pinnedBlock] },
    { label: 'eth_estimateGas', method: 'eth_estimateGas', params: [callTx, testData.pinnedBlock] },
    { label: 'eth_getLogs', method: 'eth_getLogs', params: [logsFilter] },
    { label: 'eth_getLogs(completeness)', method: 'eth_getLogs', params: [logsFilter], logsCompleteness: true },
    { label: 'eth_getBlockReceipts', method: 'eth_getBlockReceipts', params: [testData.pinnedBlock] },
    { label: 'eth_getTransactionByHash', method: 'eth_getTransactionByHash', params: [testData.txHash] },
    { label: 'eth_getTransactionReceipt', method: 'eth_getTransactionReceipt', params: [testData.txHash] },
  ];
}

// ---------------------------------------------------------------------------
// Suite
// ---------------------------------------------------------------------------

describe('RPC Integration', { skip: !RUN, timeout: PARENT_TIMEOUT, concurrency: false }, () => {
  let requests = [];

  // Node 22: before(fn, options) — options first would register a no-op hook.
  before(async () => {
    if (!GROUND_TRUTH_RPC) {
      throw new Error(`No ground-truth RPC URL for chain ${CHAIN_ID}; set C4_RPC_URL`);
    }
    const testData = await resolveTestData();
    requests = buildRequests(testData);
    if (requests.length !== REQUEST_COUNT) {
      throw new Error(`REQUEST_COUNT=${REQUEST_COUNT} but buildRequests returned ${requests.length}`);
    }
  }, { timeout: PARENT_TIMEOUT });

  for (const mode of MODES) {
    for (const privacy of PRIVACY) {
      describe(comboLabel(mode, privacy), { concurrency: false, timeout: PARENT_TIMEOUT }, () => {
        let cache;
        let client;
        let completenessClient;
        const rpcResults = new Map();

        before(async () => {
          const storage = createMemoryStorage();
          cache = createMemoryCache();
          await Colibri.register_storage(storage);
          const base = { proverMode: mode, privacyMode: privacy, cache };
          client = createClient({ ...base, logsCompleteness: false });
          completenessClient = createClient({ ...base, logsCompleteness: true });
          console.log(`\n=== ${comboLabel(mode, privacy)} ===`);
        }, { timeout: PARENT_TIMEOUT });

        test('cold cache', { timeout: RUN_TIMEOUT }, async (t) => {
          cache.clear();
          for (const req of requests) {
            await t.test(req.label, { timeout: METHOD_TIMEOUT }, async () => {
              const rpcVal = await rpcFetchWithRetry(req.method, req.params);
              const c4 = req.logsCompleteness ? completenessClient : client;
              let colibriVal;
              try {
                colibriVal = await c4.rpc(req.method, req.params);
              } catch (e) {
                const msg = e instanceof Error ? e.message : String(e);
                assert.fail(`${comboLabel(mode, privacy)} cold ${req.label}: Colibri threw: ${msg}`);
              }
              const accepted = await compareAgainstRpc(req, colibriVal, rpcVal);
              rpcResults.set(req.label, accepted);
            });
          }
        });

        test('warm cache', { timeout: RUN_TIMEOUT }, async (t) => {
          assert.ok(cache.size > 0, `${comboLabel(mode, privacy)}: HTTP cache should be populated after the cold run`);
          cache.hits = 0;
          for (const req of requests) {
            await t.test(req.label, { timeout: METHOD_TIMEOUT }, async () => {
              const rpcVal = rpcResults.get(req.label);
              assert.ok(rpcVal !== undefined, `missing RPC snapshot for ${req.label} (cold run failed?)`);
              const c4 = req.logsCompleteness ? completenessClient : client;
              let colibriVal;
              try {
                colibriVal = await c4.rpc(req.method, req.params);
              } catch (e) {
                const msg = e instanceof Error ? e.message : String(e);
                assert.fail(`${comboLabel(mode, privacy)} warm ${req.label}: Colibri threw: ${msg}`);
              }
              assertSameValues(rpcVal, colibriVal, `${req.label} (warm)`);
            });
          }
          assert.ok(cache.hits > 0, `${comboLabel(mode, privacy)}: warm run should hit the HTTP cache`);
        });
      });
    }
  }
});
