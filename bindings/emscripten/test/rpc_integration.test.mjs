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
 * C4_MODES, C4_PRIVACY, C4_COMPARE, C4_RPC_REPLAY, --rpc, --beacon, --prover,
 * --chain, --modes, --privacy, --compare, --replay, --debug
 *
 * `--chain` / `C4_CHAIN_ID` accepts a numeric id, hex id, or an alias from
 * `scripts/chain_defaults/chains.json` (mainnet, sepolia, gnosis, chiado,
 * plataberget / glamsterdam-devnet-8). Default RPC / Beacon / prover URLs
 * come from that spec when not overridden.
 *
 * `--compare` / `C4_COMPARE` (default: extra):
 *   extra   overlapping keys must match; extra keys on either side are allowed
 *   values  extra RPC keys allowed; extra non-empty Colibri keys fail (except
 *           documented OP-Stack optional fields)
 *   strict  both objects must have the same keys; extra keys on either side fail
 *
 * Live tags (`latest` / `safe` / `finalized`) are not compared as exact block
 * identity. Execution RPC treats `latest` as the current head; Colibri proves
 * the last verifiable block (head-1, soon head-2 with Gloas). Warm remote/hybrid
 * runs also re-resolve the live tag, so the chain may have moved. Block numbers
 * may lag by `LIVE_HEAD_TOLERANCE` for `latest`. `safe` / `finalized` are
 * beacon checkpoints that flip once per epoch: at the boundary Colibri (especially
 * hybrid, which caches the tag for ~one epoch) can still report the previous
 * epoch. Colibri may be *behind* by one epoch (`slots_per_epoch` + head lag),
 * or slightly *ahead* if the RPC sample is stale (`LIVE_HEAD_TOLERANCE`). A
 * Colibri result a full epoch *ahead* of RPC (e.g. `safe` served as `finalized`)
 * still fails. Block *contents* are re-checked against the block Colibri proved.
 * Scalar methods that can shift with that offset (`eth_estimateGas`) use a
 * quantity tolerance instead of equality.
 *
 * Failed tests keep a dump under
 * `test/.rpc-dumps/<run>/<combo>/<phase>/<label>/`:
 *   rpc_request.json          original Colibri RPC (method, params, combo, …)
 *   storage/ + storage_keys.json   JS storage snapshot taken *before* the call
 *   {n}_get_request.json      HTTP cache lookup (`cache.get`)
 *   {n}_request.json + {n}_response.ssz   HTTP fetch written via `cache.set`
 * Successful tests discard their dump directory. Live runs never serve cache
 * hits (`get` is empty) so each call is a real fetch.
 *
 * Replay a kept dump (serves recorded HTTP, restores storage first):
 *   C4_RPC_REPLAY=/path/to/dump C4_RUN_RPC_INTEGRATION=1 npm run test:rpc-integration
 *   --replay /path/to/dump
 */

import test, { describe, before, after } from 'node:test';
import assert from 'node:assert';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { modulePath } from './test_config.js';

const ColibriModule = await import(modulePath);
const Colibri = ColibriModule.default;

const METHOD_TIMEOUT = 180_000;
const REQUEST_COUNT = 14;
const RUN_TIMEOUT = METHOD_TIMEOUT * (REQUEST_COUNT + 2);
const PARENT_TIMEOUT = RUN_TIMEOUT * 2 + 5 * 60_000;

/**
 * Preferred mainnet-style fixtures. Used when the contract exists on the
 * target chain; otherwise `resolveTestData` discovers a live contract / logs
 * address (needed for Sepolia, Gnosis, and Platåberget / Glamsterdam).
 */
const PREFERRED_USDC = '0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48';
const PREFERRED_USDT = '0xdac17f958d2ee523a2206206994597c13d831ec7';
const BALANCE_OF = '0x70a0823100000000000000000000000037305b1cd40574e4c5ce33f8e8306be057fd7341';
const FEE_RECIPIENT = '0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5';
const IDENTITY_PRECOMPILE = '0x0000000000000000000000000000000000000004';
const IDENTITY_CALLDATA = '0x48656c6c6f';

const CHAINS_SPEC = JSON.parse(fs.readFileSync(
  path.join(path.dirname(fileURLToPath(import.meta.url)), '../../../scripts/chain_defaults/chains.json'),
  'utf8',
));

/** SSZ optional fields that JSON-RPC nodes typically omit on mainnet. */
const COLIBRI_ONLY_OPTIONAL = new Set([
  'sourceHash',
  'mint',
  'isSystemTx',
  'depositReceiptVersion',
  'depositNonce',
]);

const LIVE_TAGS = new Set(['latest', 'safe', 'finalized']);
/** Execution RPC `latest` is head; Colibri proves head-1 / head-2. */
const LIVE_HEAD_TOLERANCE = 2;

function chainFromSpec(chainId) {
  return CHAINS_SPEC.chains.find((c) => c.id === chainId) ?? null;
}

function resolveChainId(raw) {
  const s = String(raw ?? '').trim();
  if (!s) return 1;
  if (/^\d+$/.test(s)) return parseInt(s, 10);
  if (/^0x[0-9a-fA-F]+$/i.test(s)) return parseInt(s, 16);
  const lower = s.toLowerCase();
  for (const chain of CHAINS_SPEC.chains) {
    if (chain.name === lower || chain.aliases.some((a) => a.toLowerCase() === lower)) return chain.id;
  }
  throw new Error(`Unknown chain "${raw}" (use an id or alias from scripts/chain_defaults/chains.json)`);
}

/** Gnosis / Chiado use 16-slot epochs; Ethereum family (incl. Platåberget) uses 32. */
function slotsPerEpoch(chainId) {
  if (chainId === 100 || chainId === 10200) return 16;
  return 32;
}

function liveTagOf(req) {
  if (req?.method === 'eth_blockNumber') return 'latest';
  const tag = req?.params?.[0];
  return typeof tag === 'string' && LIVE_TAGS.has(tag) ? tag : null;
}

/**
 * `latest` / `eth_blockNumber`: 1–2 block prover lag.
 * `safe` / `finalized`: checkpoint tags flip once per epoch; tolerate the
 * previous epoch plus the same head lag.
 * @param tag live tag (`latest` / `safe` / `finalized`)
 * @param chainId defaults to the process chain; pass explicitly in unit tests
 */
function liveTolerance(tag, chainId = CHAIN_ID) {
  if (tag === 'finalized' || tag === 'safe') return slotsPerEpoch(chainId) + LIVE_HEAD_TOLERANCE;
  return LIVE_HEAD_TOLERANCE;
}

/**
 * `eth_estimateGas` can shift with the 1–2 block latest/head offset and, in
 * `privacy=basic`, is computed locally (evmone) rather than echoed from RPC.
 * Absolute bound covers a cold/warm account-access delta; relative bound
 * covers larger calls whose gas scales with the same offset.
 */
const ESTIMATE_GAS_ABS_TOLERANCE = 3000;
const ESTIMATE_GAS_REL_TOLERANCE = 0.05;

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

const CHAIN_ID = resolveChainId(argVal('chain', 'C4_CHAIN_ID', '1'));
const RPC_URLS = splitList(argVal('rpc', 'C4_RPC_URL', ''));
const BEACON_URLS = splitList(argVal('beacon', 'C4_BEACON_URL', ''));
const PROVER_URLS = splitList(argVal('prover', 'C4_PROVER_URL', ''));
const MODES = (argVal('modes', 'C4_MODES', 'local,remote,hybrid')).split(',').map((s) => s.trim()).filter(Boolean);
const PRIVACY = (argVal('privacy', 'C4_PRIVACY', 'none,basic')).split(',').map((s) => s.trim()).filter(Boolean);
const COMPARE_MODE = argVal('compare', 'C4_COMPARE', 'extra');
const DEBUG = process.argv.includes('--debug') || process.env.C4_DEBUG === '1';
const REPLAY_DIR = argVal('replay', 'C4_RPC_REPLAY', '');
const RUN_LIVE = process.env.C4_RUN_RPC_INTEGRATION === '1' && !REPLAY_DIR;

if (!['extra', 'values', 'strict'].includes(COMPARE_MODE)) {
  throw new Error(`Invalid --compare / C4_COMPARE="${COMPARE_MODE}" (expected extra|values|strict)`);
}

const SPEC_CHAIN = chainFromSpec(CHAIN_ID);
const GROUND_TRUTH_RPC = (RPC_URLS && RPC_URLS[0]) || SPEC_CHAIN?.eth_rpc?.[0] || null;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function createMemoryStorage() {
  const map = new Map();
  return {
    get: (key) => map.get(key) ?? null,
    set: (key, value) => { map.set(key, new Uint8Array(value)); },
    del: (key) => map.delete(key),
    _map: map,
  };
}

const DUMP_ROOT = path.join(path.dirname(fileURLToPath(import.meta.url)), '.rpc-dumps');

function sanitizeDumpName(s) {
  return String(s).replace(/[^a-zA-Z0-9._-]+/g, '_').replace(/^_|_$/g, '') || 'unnamed';
}

function serializeDataRequest(req) {
  return {
    method: req?.method,
    chain_id: req?.chain_id,
    encoding: req?.encoding,
    type: req?.type,
    exclude_mask: req?.exclude_mask,
    url: req?.url,
    payload: req?.payload,
    delay: req?.delay,
    ttl: req?.ttl,
  };
}

function cacheKey(req) {
  return JSON.stringify({
    type: req?.type,
    url: req?.url,
    method: req?.method,
    encoding: req?.encoding,
    payload: req?.payload,
  });
}

function asBytes(data) {
  return data instanceof Uint8Array ? data : new Uint8Array(data ?? []);
}

function snapshotStorage(dir, storage) {
  const dest = path.join(dir, 'storage');
  fs.mkdirSync(dest, { recursive: true });
  const keys = [];
  if (!storage?._map) {
    fs.writeFileSync(path.join(dir, 'storage_keys.json'), '[]\n');
    return;
  }
  for (const [key, value] of storage._map) {
    const bytes = asBytes(value);
    keys.push({ key, len: bytes.byteLength });
    const safe = /^[A-Za-z0-9._-]+$/.test(key) ? key : sanitizeDumpName(key);
    fs.writeFileSync(path.join(dest, safe), bytes);
  }
  fs.writeFileSync(path.join(dir, 'storage_keys.json'), JSON.stringify(keys, null, 2));
}

function loadStorageSnapshot(dir, storage) {
  const src = path.join(dir, 'storage');
  if (!fs.existsSync(src)) return 0;
  let n = 0;
  for (const name of fs.readdirSync(src)) {
    storage.set(name, new Uint8Array(fs.readFileSync(path.join(src, name))));
    n++;
  }
  return n;
}

function loadReplayResponses(dir) {
  const map = new Map();
  if (!dir || !fs.existsSync(dir)) return map;
  for (const name of fs.readdirSync(dir)) {
    const m = /^(\d+)_request\.json$/.exec(name);
    if (!m) continue;
    const req = JSON.parse(fs.readFileSync(path.join(dir, name), 'utf8'));
    const resPath = path.join(dir, `${m[1]}_response.ssz`);
    if (!fs.existsSync(resPath)) continue;
    map.set(cacheKey(req), new Uint8Array(fs.readFileSync(resPath)));
  }
  return map;
}

function readReplayMeta(dir) {
  const p = path.join(dir, 'rpc_request.json');
  if (!fs.existsSync(p)) throw new Error(`replay dump missing rpc_request.json: ${dir}`);
  return JSON.parse(fs.readFileSync(p, 'utf8'));
}

/**
 * Live: `get` is always empty (real fetch), every lookup and every `set` is
 * written into the current dump directory. Replay: `get` serves the recorded
 * `{n}_response.ssz` for a matching `{n}_request.json`.
 */
function createDumpCache({ runDir, replayDir } = {}) {
  let seq = 0;
  let currentDir = null;
  const replayMap = replayDir ? loadReplayResponses(replayDir) : null;

  return {
    cacheable: () => true,
    get(req) {
      if (currentDir && !replayDir) {
        seq += 1;
        fs.mkdirSync(currentDir, { recursive: true });
        fs.writeFileSync(
          path.join(currentDir, `${seq}_get_request.json`),
          JSON.stringify(serializeDataRequest(req), null, 2),
        );
      }
      if (replayMap) {
        const hit = replayMap.get(cacheKey(req));
        return hit ?? undefined;
      }
      return undefined;
    },
    set(req, data) {
      if (!currentDir || replayDir) return;
      seq += 1;
      fs.mkdirSync(currentDir, { recursive: true });
      const bytes = asBytes(data);
      fs.writeFileSync(path.join(currentDir, `${seq}_request.json`), JSON.stringify(serializeDataRequest(req), null, 2));
      fs.writeFileSync(path.join(currentDir, `${seq}_response.ssz`), bytes);
    },
    begin(meta, storage) {
      seq = 0;
      currentDir = replayDir
        ? replayDir
        : path.join(runDir, sanitizeDumpName(meta.combo), meta.phase, sanitizeDumpName(meta.label));
      if (!replayDir) {
        fs.rmSync(currentDir, { recursive: true, force: true });
        fs.mkdirSync(currentDir, { recursive: true });
        fs.writeFileSync(path.join(currentDir, 'rpc_request.json'), JSON.stringify({
          method: meta.method,
          params: meta.params,
          label: meta.label,
          combo: meta.combo,
          mode: meta.mode,
          privacy: meta.privacy,
          phase: meta.phase,
          logsCompleteness: !!meta.logsCompleteness,
          chainId: CHAIN_ID,
        }, null, 2));
        snapshotStorage(currentDir, storage);
      }
    },
    keep(errorMsg) {
      if (!currentDir) return null;
      if (!replayDir) {
        fs.mkdirSync(currentDir, { recursive: true });
        if (errorMsg) fs.writeFileSync(path.join(currentDir, '_error.txt'), String(errorMsg));
        const hasHttp = fs.readdirSync(currentDir).some((n) => /^\d+_response\.ssz$/.test(n));
        if (!hasHttp) {
          fs.writeFileSync(
            path.join(currentDir, '_note.txt'),
            'No HTTP cache.set during this call. The result likely came from the in-process EL header cache (not JS storage).\n',
          );
        }
      }
      const kept = currentDir;
      currentDir = null;
      return kept;
    },
    discard() {
      if (currentDir && !replayDir) fs.rmSync(currentDir, { recursive: true, force: true });
      currentDir = null;
    },
  };
}

async function withDump(cache, storage, meta, fn) {
  cache.begin(meta, storage);
  try {
    await fn();
    cache.discard();
  } catch (e) {
    const dir = cache.keep(e instanceof Error ? e.message : String(e));
    if (dir && !REPLAY_DIR) console.error(`  kept request dump: ${dir}`);
    throw e;
  }
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
    // Persist the in-process EL header cache / tags into JS storage so dumps
    // can replay hybrid `last_block_hash` and cache hits.
    persist_header_cache: true,
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

/** Positive = Colibri is behind the RPC sample. */
function signedBlockLag(rpcVal, colibriVal) {
  const rpc = parseHexQuantity(rpcVal);
  const c4 = parseHexQuantity(colibriVal);
  if (!Number.isFinite(rpc) || !Number.isFinite(c4)) return Infinity;
  return rpc - c4;
}

/**
 * Live-tag number window: Colibri may lag by `liveTolerance(tag)` and may lead
 * by `LIVE_HEAD_TOLERANCE` (stale RPC sample). Absolute equality is not required.
 */
function withinLiveTolerance(rpcVal, colibriVal, tag, chainId = CHAIN_ID) {
  const lag = signedBlockLag(rpcVal, colibriVal);
  if (!Number.isFinite(lag) || lag === Infinity) return false;
  return lag <= liveTolerance(tag, chainId) && lag >= -LIVE_HEAD_TOLERANCE;
}

function quantityWithinTolerance(rpcVal, colibriVal, absTol, relTol) {
  const a = parseHexQuantity(rpcVal);
  const b = parseHexQuantity(colibriVal);
  if (!Number.isFinite(a) || !Number.isFinite(b)) return false;
  const delta = Math.abs(a - b);
  if (delta <= absTol) return true;
  return delta / Math.max(a, b, 1) <= relTol;
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

function failLiveLag(label, rpcVal, colibriVal, tag, chainId = CHAIN_ID) {
  const lag = signedBlockLag(rpcVal, colibriVal);
  const maxBehind = liveTolerance(tag, chainId);
  assert.fail(
    `${label}: live lag ${lag} not in [-${LIVE_HEAD_TOLERANCE}, ${maxBehind}] ` +
    `(RPC=${fmtVal(rpcVal)} Colibri=${fmtVal(colibriVal)})`,
  );
}

async function assertLiveHeadQuantity(label, rpcVal, colibriVal, tag = 'latest') {
  if (withinLiveTolerance(rpcVal, colibriVal, tag)) return;
  const retry = await rpcFetchWithRetry('eth_blockNumber', []);
  if (withinLiveTolerance(retry, colibriVal, tag)) return;
  failLiveLag(label, retry, colibriVal, tag);
}

async function compareLiveBlock(req, colibriVal, rpcVal) {
  const tag = liveTagOf(req);
  try {
    assertSameValues(rpcVal, colibriVal, req.label);
    return rpcVal;
  } catch {
    const includeTxs = req.params[1];
    const rpcNum = rpcVal && typeof rpcVal === 'object' ? rpcVal.number : undefined;
    const c4Num = colibriVal && typeof colibriVal === 'object' ? colibriVal.number : undefined;
    if (!withinLiveTolerance(rpcNum, c4Num, tag)) {
      const retry = await rpcFetchWithRetry(req.method, req.params);
      try {
        assertSameValues(retry, colibriVal, req.label + ' (rpc retry)');
        return retry;
      } catch {
        const retryNum = retry && retry.number;
        if (!withinLiveTolerance(retryNum, c4Num, tag)) {
          failLiveLag(req.label, retryNum, c4Num, tag);
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

async function compareQuantity(req, colibriVal, rpcVal) {
  const absTol = req.quantityAbsTolerance ?? 0;
  const relTol = req.quantityRelTolerance ?? 0;
  if (quantityWithinTolerance(rpcVal, colibriVal, absTol, relTol)) return colibriVal;
  const retry = await rpcFetchWithRetry(req.method, req.params);
  if (quantityWithinTolerance(retry, colibriVal, absTol, relTol)) return colibriVal;
  const delta = blockDelta(retry, colibriVal);
  assert.fail(
    `${req.label}: quantity delta ${delta} exceeds abs=${absTol} rel=${relTol} ` +
    `(RPC=${fmtVal(retry)} Colibri=${fmtVal(colibriVal)})`,
  );
}

async function compareAgainstRpc(req, colibriVal, rpcVal) {
  if (req.method === 'eth_blockNumber') {
    await assertLiveHeadQuantity(req.label, rpcVal, colibriVal, 'latest');
    return colibriVal;
  }
  if (req.method === 'eth_getBlockByNumber' && isLiveRequest(req)) {
    return compareLiveBlock(req, colibriVal, rpcVal);
  }
  if (req.quantityAbsTolerance != null || req.quantityRelTolerance != null) {
    return compareQuantity(req, colibriVal, rpcVal);
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
  console.log(`  Chain: ${CHAIN_ID}${SPEC_CHAIN ? ` (${SPEC_CHAIN.name})` : ''}`);
  if (RPC_URLS) console.log(`  Colibri rpcs: ${RPC_URLS.join(', ')}`);
  if (BEACON_URLS) console.log(`  Colibri beacon: ${BEACON_URLS.join(', ')}`);
  if (PROVER_URLS) console.log(`  Colibri prover: ${PROVER_URLS.join(', ')}`);
  else console.log('  Colibri prover: (client default / empty for local)');
  console.log(`  Modes: ${MODES.join(', ')}`);
  console.log(`  Privacy: ${PRIVACY.join(', ')}`);
  console.log(`  Compare: ${COMPARE_MODE}`);
  console.log(`  Live tolerance: latest=${liveTolerance('latest')} safe/finalized=${liveTolerance('finalized')} (epoch=${slotsPerEpoch(CHAIN_ID)})`);

  const blockNumHex = await rpcFetchWithRetry('eth_blockNumber', []);
  const currentBlock = parseInt(blockNumHex, 16) - 2;
  console.log(`  Head-2: ${currentBlock} (${toHexBlock(currentBlock)})`);

  let pinnedBlock = null;
  let txHash = null;
  let pinnedBlockObj = null;
  for (let i = 0; i < 20; i++) {
    const hex = toHexBlock(currentBlock - i);
    const block = await rpcFetchWithRetry('eth_getBlockByNumber', [hex, true]);
    const hash = txHashFromBlock(block);
    if (hash) {
      pinnedBlock = hex;
      txHash = hash;
      pinnedBlockObj = block;
      break;
    }
  }
  if (!pinnedBlock || !txHash) throw new Error('No transactions in the last 20 blocks');
  console.log(`  Pinned block: ${pinnedBlock}`);
  console.log(`  TX: ${txHash}`);

  const codeAddress = await resolveCodeAddress(pinnedBlock, pinnedBlockObj, currentBlock);
  const callTx = await resolveCallTx(pinnedBlock, codeAddress);
  const { logsBlock, logsAddress } = await resolveLogsTarget(pinnedBlock, currentBlock);
  console.log(`  Code address: ${codeAddress}`);
  console.log(`  Call: ${callTx.to} data=${callTx.data.slice(0, 18)}…`);

  return { pinnedBlock, txHash, logsBlock, logsAddress, codeAddress, callTx };
}

function hasContractCode(code) {
  if (typeof code !== 'string') return false;
  const hex = code.toLowerCase();
  return hex.startsWith('0x') && hex !== '0x' && hex !== '0x0';
}

/**
 * Offline decision for `eth_call` / `eth_estimateGas` fixtures.
 * Preferred USDC keeps `balanceOf`; every other chain uses the identity
 * precompile when it is available, otherwise an empty call to `codeAddress`.
 */
function pickCallTx(codeAddress, identityAvailable = true) {
  if (typeof codeAddress === 'string' && codeAddress.toLowerCase() === PREFERRED_USDC.toLowerCase()) {
    return { to: PREFERRED_USDC, data: BALANCE_OF };
  }
  if (identityAvailable) return { to: IDENTITY_PRECOMPILE, data: IDENTITY_CALLDATA };
  return { to: codeAddress, data: '0x' };
}

async function resolveCodeAddress(pinnedBlock, pinnedBlockObj, currentBlock) {
  const preferred = await rpcFetchWithRetry('eth_getCode', [PREFERRED_USDC, pinnedBlock]);
  if (hasContractCode(preferred)) return PREFERRED_USDC;

  const candidates = [];
  for (const tx of pinnedBlockObj?.transactions || []) {
    if (tx && typeof tx === 'object' && tx.to) candidates.push(tx.to);
  }
  for (let i = 0; i < 15 && candidates.length < 20; i++) {
    const hex = toHexBlock(currentBlock - i);
    if (hex === pinnedBlock) continue;
    const block = await rpcFetchWithRetry('eth_getBlockByNumber', [hex, true]);
    for (const tx of block?.transactions || []) {
      if (tx && typeof tx === 'object' && tx.to) candidates.push(tx.to);
    }
  }
  const seen = new Set();
  for (const addr of candidates) {
    const key = addr.toLowerCase();
    if (seen.has(key)) continue;
    seen.add(key);
    const code = await rpcFetchWithRetry('eth_getCode', [addr, pinnedBlock]);
    if (hasContractCode(code)) return addr;
  }
  console.log('  No live contract found; eth_getCode will use the identity precompile (empty code)');
  return IDENTITY_PRECOMPILE;
}

async function resolveCallTx(pinnedBlock, codeAddress) {
  const preferred = pickCallTx(codeAddress, true);
  if (preferred.to.toLowerCase() === PREFERRED_USDC.toLowerCase()) return preferred;
  // Identity precompile echoes calldata on every EVM chain, including plataberget.
  try {
    await rpcFetchWithRetry('eth_call', [preferred, pinnedBlock]);
    return preferred;
  } catch {
    return pickCallTx(codeAddress, false);
  }
}

async function resolveLogsTarget(pinnedBlock, currentBlock) {
  for (let i = 0; i < 30; i++) {
    const hex = toHexBlock(currentBlock - i);
    const logs = await rpcFetchWithRetry('eth_getLogs', [{
      address: [PREFERRED_USDT],
      fromBlock: hex,
      toBlock: hex,
    }]);
    if (Array.isArray(logs) && logs.length > 0) {
      console.log(`  Logs block: ${hex} (${logs.length} USDT logs)`);
      return { logsBlock: hex, logsAddress: PREFERRED_USDT };
    }
  }
  for (let i = 0; i < 30; i++) {
    const hex = toHexBlock(currentBlock - i);
    const logs = await rpcFetchWithRetry('eth_getLogs', [{ fromBlock: hex, toBlock: hex }]);
    if (Array.isArray(logs) && logs.length > 0) {
      const addr = logs[0]?.address;
      console.log(`  Logs block: ${hex} (${logs.length} logs${addr ? ` at ${addr}` : ''})`);
      return { logsBlock: hex, logsAddress: addr || null };
    }
  }
  console.log(`  Logs block: ${pinnedBlock} (may be empty)`);
  return { logsBlock: pinnedBlock, logsAddress: null };
}

function buildRequests(testData) {
  const logsFilter = {
    fromBlock: testData.logsBlock,
    toBlock: testData.logsBlock,
  };
  if (testData.logsAddress) logsFilter.address = [testData.logsAddress];
  return [
    { label: 'eth_chainId', method: 'eth_chainId', params: [] },
    { label: 'eth_blockNumber', method: 'eth_blockNumber', params: [], live: true },
    { label: 'eth_getBlockByNumber(latest)', method: 'eth_getBlockByNumber', params: ['latest', false] },
    { label: 'eth_getBlockByNumber(safe)', method: 'eth_getBlockByNumber', params: ['safe', false] },
    { label: 'eth_getBlockByNumber(finalized)', method: 'eth_getBlockByNumber', params: ['finalized', false] },
    { label: 'eth_getBalance', method: 'eth_getBalance', params: [FEE_RECIPIENT, testData.pinnedBlock] },
    { label: 'eth_getCode', method: 'eth_getCode', params: [testData.codeAddress, testData.pinnedBlock] },
    { label: 'eth_call', method: 'eth_call', params: [testData.callTx, testData.pinnedBlock] },
    {
      label: 'eth_estimateGas',
      method: 'eth_estimateGas',
      params: [testData.callTx, testData.pinnedBlock],
      quantityAbsTolerance: ESTIMATE_GAS_ABS_TOLERANCE,
      quantityRelTolerance: ESTIMATE_GAS_REL_TOLERANCE,
    },
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

describe('RPC Integration helpers', () => {
  test('resolves chain aliases from chains.json', () => {
    assert.strictEqual(resolveChainId('1'), 1);
    assert.strictEqual(resolveChainId(' 1 '), 1);
    assert.strictEqual(resolveChainId(''), 1);
    assert.strictEqual(resolveChainId(undefined), 1);
    assert.strictEqual(resolveChainId('mainnet'), 1);
    assert.strictEqual(resolveChainId('MAINNET'), 1);
    assert.strictEqual(resolveChainId('eth'), 1);
    assert.strictEqual(resolveChainId('0x1'), 1);
    assert.strictEqual(resolveChainId('sepolia'), 11155111);
    assert.strictEqual(resolveChainId('0xaa36a7'), 11155111);
    assert.strictEqual(resolveChainId('gnosis'), 100);
    assert.strictEqual(resolveChainId('xdai'), 100);
    assert.strictEqual(resolveChainId('0x64'), 100);
    assert.strictEqual(resolveChainId('chiado'), 10200);
    assert.strictEqual(resolveChainId('plataberget'), 7091047534);
    assert.strictEqual(resolveChainId('Plataberget'), 7091047534);
    assert.strictEqual(resolveChainId('glamsterdam-devnet-8'), 7091047534);
    assert.strictEqual(resolveChainId('0x1a6a8cc6e'), 7091047534);
    assert.strictEqual(resolveChainId(7091047534), 7091047534);
    assert.ok(chainFromSpec(7091047534)?.eth_rpc?.[0]);
    assert.ok(chainFromSpec(100)?.eth_rpc?.[0]);
    assert.strictEqual(chainFromSpec(999), null);
  });

  test('unknown chain alias throws', () => {
    assert.throws(() => resolveChainId('not-a-chain'), /Unknown chain "not-a-chain"/);
    assert.throws(() => resolveChainId('holesky'), /Unknown chain "holesky"/);
  });

  test('checkpoint tags tolerate one epoch plus head lag', () => {
    assert.strictEqual(slotsPerEpoch(1), 32);
    assert.strictEqual(slotsPerEpoch(7091047534), 32);
    assert.strictEqual(slotsPerEpoch(100), 16);
    assert.strictEqual(slotsPerEpoch(10200), 16);
    assert.strictEqual(slotsPerEpoch(11155111), 32);

    assert.strictEqual(liveTolerance('latest'), LIVE_HEAD_TOLERANCE);
    assert.strictEqual(liveTolerance('latest', 100), LIVE_HEAD_TOLERANCE);

    assert.strictEqual(liveTolerance('finalized', 1), 32 + LIVE_HEAD_TOLERANCE);
    assert.strictEqual(liveTolerance('safe', 7091047534), 32 + LIVE_HEAD_TOLERANCE);
    assert.strictEqual(liveTolerance('finalized', 100), 16 + LIVE_HEAD_TOLERANCE);
    assert.strictEqual(liveTolerance('safe', 10200), 16 + LIVE_HEAD_TOLERANCE);
    assert.strictEqual(liveTolerance('finalized'), liveTolerance('finalized', CHAIN_ID));

    // Observed hybrid miss: RPC finalized is one epoch ahead of Colibri.
    assert.strictEqual(signedBlockLag('0x18b0c5a', '0x18b0c3a'), 32);
    assert.ok(withinLiveTolerance('0x18b0c5a', '0x18b0c3a', 'finalized', 1));
    assert.ok(withinLiveTolerance('0x18b0c5a', '0x18b0c3a', 'finalized', 7091047534));
    // Same numeric gap is two Gnosis epochs — must not be accepted there.
    assert.ok(!withinLiveTolerance('0x18b0c5a', '0x18b0c3a', 'finalized', 100));
    assert.ok(withinLiveTolerance('0x100', '0xf0', 'finalized', 100));
    // Colibri a full epoch *ahead* is safe-served-as-finalized, not lag.
    assert.ok(!withinLiveTolerance('0x18b0c3a', '0x18b0c5a', 'finalized', 1));
    assert.ok(withinLiveTolerance('0x10', '0x12', 'latest'));
    assert.ok(!withinLiveTolerance('0x10', '0x13', 'latest'));
  });

  test('liveTagOf maps blockNumber and checkpoint params', () => {
    assert.strictEqual(liveTagOf({ method: 'eth_blockNumber', params: [] }), 'latest');
    assert.strictEqual(liveTagOf({ method: 'eth_getBlockByNumber', params: ['latest', false] }), 'latest');
    assert.strictEqual(liveTagOf({ method: 'eth_getBlockByNumber', params: ['safe', false] }), 'safe');
    assert.strictEqual(liveTagOf({ method: 'eth_getBlockByNumber', params: ['finalized', false] }), 'finalized');
    assert.strictEqual(liveTagOf({ method: 'eth_getBlockByNumber', params: ['0x10', false] }), null);
    assert.strictEqual(liveTagOf({ method: 'eth_getBalance', params: [FEE_RECIPIENT, 'latest'] }), null);
    assert.strictEqual(liveTolerance(liveTagOf({ method: 'eth_getBlockByNumber', params: ['finalized', false] }), 1), 34);
  });

  test('empty code falls back to identity precompile call data', () => {
    assert.strictEqual(hasContractCode('0x6080604052'), true);
    assert.strictEqual(hasContractCode('0x00'), true);
    assert.strictEqual(hasContractCode('0x'), false);
    assert.strictEqual(hasContractCode('0x0'), false);
    assert.strictEqual(hasContractCode('0X0'), false);
    assert.strictEqual(hasContractCode(null), false);
    assert.strictEqual(hasContractCode(undefined), false);
    assert.strictEqual(hasContractCode(''), false);

    assert.deepStrictEqual(pickCallTx(PREFERRED_USDC), { to: PREFERRED_USDC, data: BALANCE_OF });
    assert.deepStrictEqual(pickCallTx(PREFERRED_USDC.toLowerCase()), { to: PREFERRED_USDC, data: BALANCE_OF });
    assert.deepStrictEqual(pickCallTx('0x1111111111111111111111111111111111111111'), {
      to: IDENTITY_PRECOMPILE,
      data: IDENTITY_CALLDATA,
    });
    assert.deepStrictEqual(pickCallTx(IDENTITY_PRECOMPILE, true), {
      to: IDENTITY_PRECOMPILE,
      data: IDENTITY_CALLDATA,
    });
    assert.deepStrictEqual(pickCallTx(IDENTITY_PRECOMPILE, false), {
      to: IDENTITY_PRECOMPILE,
      data: '0x',
    });
    assert.deepStrictEqual(pickCallTx('0xAbc', false), { to: '0xAbc', data: '0x' });
  });

  test('buildRequests wires discovered fixtures without network', () => {
    const callTx = pickCallTx('0x1111111111111111111111111111111111111111');
    const reqs = buildRequests({
      pinnedBlock: '0x10',
      txHash: '0xabc',
      logsBlock: '0xf',
      logsAddress: PREFERRED_USDT,
      codeAddress: IDENTITY_PRECOMPILE,
      callTx,
    });
    const byLabel = Object.fromEntries(reqs.map((r) => [r.label, r]));
    assert.strictEqual(byLabel['eth_getCode'].params[0], IDENTITY_PRECOMPILE);
    assert.deepStrictEqual(byLabel['eth_call'].params[0], callTx);
    assert.deepStrictEqual(byLabel['eth_estimateGas'].params[0], callTx);
    assert.deepStrictEqual(byLabel['eth_getLogs'].params[0].address, [PREFERRED_USDT]);
    assert.strictEqual(byLabel['eth_getLogs'].params[0].fromBlock, '0xf');

    const unfiltered = buildRequests({
      pinnedBlock: '0x10',
      txHash: '0xabc',
      logsBlock: '0xf',
      logsAddress: null,
      codeAddress: IDENTITY_PRECOMPILE,
      callTx,
    });
    assert.strictEqual(unfiltered.find((r) => r.label === 'eth_getLogs').params[0].address, undefined);
  });
});

describe('RPC Integration replay', { skip: !REPLAY_DIR, timeout: METHOD_TIMEOUT + 60_000, concurrency: false }, () => {
  test('replay recorded dump', { timeout: METHOD_TIMEOUT }, async () => {
    const dir = path.resolve(REPLAY_DIR);
    const meta = readReplayMeta(dir);
    const storage = createMemoryStorage();
    const loaded = loadStorageSnapshot(dir, storage);
    await Colibri.register_storage(storage);
    const cache = createDumpCache({ replayDir: dir });
    const client = createClient({
      proverMode: meta.mode,
      privacyMode: meta.privacy,
      cache,
      logsCompleteness: !!meta.logsCompleteness,
    });
    console.log(`  Replaying ${dir}`);
    console.log(`  ${meta.combo || comboLabel(meta.mode, meta.privacy)} ${meta.phase} ${meta.label}`);
    console.log(`  Restored ${loaded} storage key(s), ${loadReplayResponses(dir).size} HTTP response(s)`);
    await withDump(cache, storage, meta, async () => {
      await client.rpc(meta.method, meta.params);
    });
  });
});

describe('RPC Integration', { skip: !RUN_LIVE, timeout: PARENT_TIMEOUT, concurrency: false }, () => {
  let requests = [];
  let dumpRunDir = '';

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
    dumpRunDir = path.join(DUMP_ROOT, new Date().toISOString().replace(/[:.]/g, '-'));
    fs.mkdirSync(dumpRunDir, { recursive: true });
    console.log(`  Request dumps (failed tests only): ${dumpRunDir}`);
  }, { timeout: PARENT_TIMEOUT });

  after(() => {
    if (!dumpRunDir || !fs.existsSync(dumpRunDir)) return;
    const walk = (dir) => {
      for (const ent of fs.readdirSync(dir, { withFileTypes: true })) {
        if (ent.isFile()) return true;
        if (ent.isDirectory() && walk(path.join(dir, ent.name))) return true;
      }
      return false;
    };
    if (!walk(dumpRunDir)) fs.rmSync(dumpRunDir, { recursive: true, force: true });
  });

  for (const mode of MODES) {
    for (const privacy of PRIVACY) {
      describe(comboLabel(mode, privacy), { concurrency: false, timeout: PARENT_TIMEOUT }, () => {
        let cache;
        let storage;
        let client;
        let completenessClient;
        const rpcResults = new Map();

        before(async () => {
          storage = createMemoryStorage();
          cache = createDumpCache({ runDir: dumpRunDir });
          await Colibri.register_storage(storage);
          const base = { proverMode: mode, privacyMode: privacy, cache };
          client = createClient({ ...base, logsCompleteness: false });
          completenessClient = createClient({ ...base, logsCompleteness: true });
          console.log(`\n=== ${comboLabel(mode, privacy)} ===`);
        }, { timeout: PARENT_TIMEOUT });

        test('cold cache', { timeout: RUN_TIMEOUT }, async (t) => {
          for (const req of requests) {
            await t.test(req.label, { timeout: METHOD_TIMEOUT }, async () => {
              await withDump(cache, storage, {
                combo: comboLabel(mode, privacy),
                phase: 'cold',
                label: req.label,
                method: req.method,
                params: req.params,
                mode,
                privacy,
                logsCompleteness: !!req.logsCompleteness,
              }, async () => {
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
            });
          }
        });

        test('warm cache', { timeout: RUN_TIMEOUT }, async (t) => {
          for (const req of requests) {
            await t.test(req.label, { timeout: METHOD_TIMEOUT }, async () => {
              await withDump(cache, storage, {
                combo: comboLabel(mode, privacy),
                phase: 'warm',
                label: req.label,
                method: req.method,
                params: req.params,
                mode,
                privacy,
                logsCompleteness: !!req.logsCompleteness,
              }, async () => {
                const c4 = req.logsCompleteness ? completenessClient : client;
                let colibriVal;
                try {
                  colibriVal = await c4.rpc(req.method, req.params);
                } catch (e) {
                  const msg = e instanceof Error ? e.message : String(e);
                  assert.fail(`${comboLabel(mode, privacy)} warm ${req.label}: Colibri threw: ${msg}`);
                }
                // Live tags re-resolve on remote/hybrid (new proof, chain may have
                // moved). Compare against a fresh RPC sample with head tolerance,
                // not against the cold snapshot. Pinned methods must match the
                // accepted cold value exactly (cache consistency).
                if (isLiveRequest(req)) {
                  const rpcVal = await rpcFetchWithRetry(req.method, req.params);
                  await compareAgainstRpc(req, colibriVal, rpcVal);
                  return;
                }
                const rpcVal = rpcResults.get(req.label);
                assert.ok(rpcVal !== undefined, `missing RPC snapshot for ${req.label} (cold run failed?)`);
                assertSameValues(rpcVal, colibriVal, `${req.label} (warm)`);
              });
            });
          }
        });
      });
    }
  }
});
