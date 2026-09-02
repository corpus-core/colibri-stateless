#!/usr/bin/env node
/**
 * Probe default chain endpoints from chains.json.
 *
 * Checks:
 *   eth_rpc      POST eth_chainId (must match), then node_check execution suite
 *   beacon_api   node_check beacon suite
 *   checkpointz  GET  /eth/v1/beacon/states/head/finality_checkpoints
 *   prover       node_check colibri suite
 *
 * node_check is loaded from build/node_check (cloned on demand, no npm dependency).
 *
 * Usage:
 *   node scripts/chain_defaults/check_endpoints.js
 *   node scripts/chain_defaults/check_endpoints.js --chain plataberget
 */

const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const { pathToFileURL } = require('url');

const ROOT = path.resolve(__dirname, '../..');
const SPEC = JSON.parse(fs.readFileSync(path.join(__dirname, 'chains.json'), 'utf8'));
const TIMEOUT_MS = 12000;
const NODE_CHECK_DIR = path.join(ROOT, 'build', 'node_check');
const NODE_CHECK_LIB = path.join(NODE_CHECK_DIR, 'src', 'lib.mjs');
const NODE_CHECK_REPO = 'https://github.com/corpus-core/node_check.git';

const chainFilter = (() => {
  const idx = process.argv.indexOf('--chain');
  return idx >= 0 ? process.argv[idx + 1] : null;
})();

function withTimeout(ms) {
  const ctrl = new AbortController();
  const id = setTimeout(() => ctrl.abort(), ms);
  return { signal: ctrl.signal, cancel: () => clearTimeout(id) };
}

async function fetchJson(url, options) {
  const t = withTimeout(TIMEOUT_MS);
  try {
    const res = await fetch(url, { ...options, signal: t.signal, redirect: 'follow' });
    const text = await res.text();
    let body = text;
    try { body = JSON.parse(text); } catch (_) { /* keep text */ }
    return { ok: res.ok, status: res.status, body };
  } catch (err) {
    return { ok: false, status: 0, body: String(err && err.message ? err.message : err) };
  } finally {
    t.cancel();
  }
}

function joinUrl(base, suffix) {
  return `${base.replace(/\/+$/, '')}/${suffix.replace(/^\/+/, '')}`;
}

function siblingNodeCheckLib() {
  const siblingLib = path.resolve(ROOT, '..', 'node_check', 'src', 'lib.mjs');
  return fs.existsSync(siblingLib) ? siblingLib : null;
}

function cloneSource() {
  const siblingRepo = path.resolve(ROOT, '..', 'node_check');
  if (fs.existsSync(path.join(siblingRepo, '.git'))) return siblingRepo;
  return NODE_CHECK_REPO;
}

function ensureNodeCheck() {
  if (fs.existsSync(NODE_CHECK_LIB)) return NODE_CHECK_LIB;

  if (!fs.existsSync(NODE_CHECK_DIR)) {
    fs.mkdirSync(path.dirname(NODE_CHECK_DIR), { recursive: true });
    const source = cloneSource();
    console.log(`Cloning node_check from ${source} -> ${NODE_CHECK_DIR}`);
    execFileSync('git', ['clone', '--depth', '1', source, NODE_CHECK_DIR], { stdio: 'inherit' });
  }

  if (fs.existsSync(NODE_CHECK_LIB)) return NODE_CHECK_LIB;

  const siblingLib = siblingNodeCheckLib();
  if (siblingLib) {
    console.warn(`cloned node_check is missing src/lib.mjs; using sibling ${siblingLib}`);
    return siblingLib;
  }

  throw new Error(`node_check src/lib.mjs not found in ${NODE_CHECK_DIR}`);
}

async function loadNodeCheck() {
  return import(pathToFileURL(ensureNodeCheck()).href);
}

async function checkEthChainId(url, chainId) {
  const r = await fetchJson(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', Accept: 'application/json' },
    body: JSON.stringify({ jsonrpc: '2.0', id: 1, method: 'eth_chainId', params: [] }),
  });
  if (!r.ok) {
    return { name: 'chainId', passed: false, result: `HTTP ${r.status} ${typeof r.body === 'string' ? r.body.slice(0, 120) : ''}` };
  }
  const hex = r.body && r.body.result;
  if (typeof hex !== 'string') {
    return { name: 'chainId', passed: false, result: `no result: ${JSON.stringify(r.body).slice(0, 120)}` };
  }
  const got = BigInt(hex);
  if (got !== BigInt(chainId)) {
    return { name: 'chainId', passed: false, result: `chainId ${hex} != ${chainId}` };
  }
  return { name: 'chainId', passed: true, result: hex };
}

async function checkCheckpointz(url) {
  const r = await fetchJson(joinUrl(url, 'eth/v1/beacon/states/head/finality_checkpoints'), {
    method: 'GET',
    headers: { Accept: 'application/json' },
  });
  if (!r.ok) return { ok: false, checks: [{ name: 'finality_checkpoints', passed: false, result: `HTTP ${r.status}` }] };
  if (!r.body || !r.body.data) {
    return { ok: false, checks: [{ name: 'finality_checkpoints', passed: false, result: 'missing data' }] };
  }
  return { ok: true, checks: [{ name: 'finality_checkpoints', passed: true, result: 'ok' }] };
}

async function runNodeCheck(checkFn, url) {
  try {
    const results = await checkFn(url);
    const suitability = results.find((r) => r.name === 'colibri suitable');
    const ok = suitability ? suitability.passed : results.every((r) => r.passed);
    return { ok, checks: results };
  } catch (err) {
    return { ok: false, checks: [{ name: 'node_check', passed: false, result: err.message }] };
  }
}

async function checkEthRpc(url, chainId, check_execution_node) {
  const chainIdCheck = await checkEthChainId(url, chainId);
  if (!chainIdCheck.passed) {
    return { ok: false, checks: [chainIdCheck] };
  }
  const suite = await runNodeCheck(check_execution_node, url);
  return { ok: suite.ok, checks: [chainIdCheck, ...suite.checks] };
}

const useColor = Boolean(process.stdout.isTTY) && !process.env.NO_COLOR && process.env.TERM !== 'dumb';

const ansi = {
  reset: '\x1b[0m',
  green: '\x1b[32m',
  red: '\x1b[31m',
  bold: '\x1b[1m',
  dim: '\x1b[2m',
};

function paint(code, text) {
  return useColor ? `${code}${text}${ansi.reset}` : text;
}

function statusMark(ok) {
  return ok ? paint(ansi.green, 'OK  ') : paint(ansi.red, 'FAIL');
}

function printEndpoint(kind, url, ok, checks) {
  console.log(`  [${statusMark(ok)}] ${kind.padEnd(12)} ${url}`);
  if (!checks || !checks.length) return;
  const width = Math.max(...checks.map((c) => c.name.length));
  for (const check of checks) {
    const result = check.passed ? paint(ansi.dim, String(check.result)) : paint(ansi.red, String(check.result));
    console.log(`           [${statusMark(check.passed)}] ${check.name.padEnd(width)}  ${result}`);
  }
}

async function run() {
  const chains = SPEC.chains.filter((c) => !chainFilter || c.name === chainFilter || String(c.id) === chainFilter);
  if (chains.length === 0) {
    console.error(`No chain matched --chain ${chainFilter}`);
    process.exit(1);
  }

  const { check_beacon_node, check_execution_node, check_colibri_node } = await loadNodeCheck();

  let failed = 0;
  for (const chain of chains) {
    console.log(paint(ansi.bold, `\n=== ${chain.name} (${chain.id}) ===`));
    const jobs = [];
    for (const url of chain.eth_rpc) {
      jobs.push(['eth_rpc', url, () => checkEthRpc(url, chain.id, check_execution_node)]);
    }
    for (const url of chain.beacon_api) {
      jobs.push(['beacon_api', url, () => runNodeCheck(check_beacon_node, url)]);
    }
    for (const url of chain.checkpointz) {
      jobs.push(['checkpointz', url, () => checkCheckpointz(url)]);
    }
    for (const url of chain.prover) {
      jobs.push(['prover', url, () => runNodeCheck(check_colibri_node, url)]);
    }

    const results = await Promise.all(jobs.map(async ([kind, url, fn]) => {
      const r = await fn();
      return { kind, url, ...r };
    }));

    for (const r of results) {
      if (!r.ok) failed++;
      printEndpoint(r.kind, r.url, r.ok, r.checks);
    }
  }

  if (failed) {
    console.log(paint(ansi.red, `\n${failed} endpoint(s) failed.`));
  } else {
    console.log(paint(ansi.green, '\nAll probed endpoints responded as expected.'));
  }
  process.exit(failed ? 1 : 0);
}

run().catch((err) => {
  console.error(err && err.message ? err.message : err);
  process.exit(1);
});
