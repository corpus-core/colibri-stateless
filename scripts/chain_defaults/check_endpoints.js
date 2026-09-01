#!/usr/bin/env node
/**
 * Probe default chain endpoints from chains.json.
 *
 * Checks:
 *   eth_rpc      POST eth_chainId  -- result must match the chain id
 *   beacon_api   GET  /eth/v1/beacon/headers/head
 *   checkpointz  GET  /eth/v1/beacon/states/head/finality_checkpoints
 *   prover       POST eth_blockNumber
 *
 * Usage:
 *   node scripts/chain_defaults/check_endpoints.js
 *   node scripts/chain_defaults/check_endpoints.js --chain plataberget
 */

const fs = require('fs');
const path = require('path');

const SPEC = JSON.parse(fs.readFileSync(path.join(__dirname, 'chains.json'), 'utf8'));
const TIMEOUT_MS = 12000;
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

async function checkEthRpc(url, chainId) {
  const r = await fetchJson(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', Accept: 'application/json' },
    body: JSON.stringify({ jsonrpc: '2.0', id: 1, method: 'eth_chainId', params: [] }),
  });
  if (!r.ok) return { ok: false, detail: `HTTP ${r.status} ${typeof r.body === 'string' ? r.body.slice(0, 120) : ''}` };
  const hex = r.body && r.body.result;
  if (typeof hex !== 'string') return { ok: false, detail: `no result: ${JSON.stringify(r.body).slice(0, 120)}` };
  const got = BigInt(hex);
  if (got !== BigInt(chainId)) return { ok: false, detail: `chainId ${hex} != ${chainId}` };
  return { ok: true, detail: hex };
}

async function checkBeacon(url, pathSuffix) {
  const r = await fetchJson(joinUrl(url, pathSuffix), {
    method: 'GET',
    headers: { Accept: 'application/json' },
  });
  if (!r.ok) return { ok: false, detail: `HTTP ${r.status}` };
  if (!r.body || !r.body.data) return { ok: false, detail: 'missing data' };
  return { ok: true, detail: 'ok' };
}

async function checkProver(url) {
  // Colibri provers answer eth_blockNumber with an SSZ proof (HTTP 200, binary).
  const r = await fetchJson(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', Accept: 'application/octet-stream' },
    body: JSON.stringify({ jsonrpc: '2.0', id: 1, method: 'eth_blockNumber', params: [] }),
  });
  if (!r.ok) return { ok: false, detail: `HTTP ${r.status}${typeof r.body === 'string' ? ' ' + r.body.slice(0, 80) : ''}` };
  return { ok: true, detail: 'ok' };
}

async function run() {
  const chains = SPEC.chains.filter((c) => !chainFilter || c.name === chainFilter || String(c.id) === chainFilter);
  if (chains.length === 0) {
    console.error(`No chain matched --chain ${chainFilter}`);
    process.exit(1);
  }

  let failed = 0;
  for (const chain of chains) {
    console.log(`\n=== ${chain.name} (${chain.id}) ===`);
    const jobs = [];
    for (const url of chain.eth_rpc) jobs.push(['eth_rpc', url, () => checkEthRpc(url, chain.id)]);
    for (const url of chain.beacon_api) jobs.push(['beacon_api', url, () => checkBeacon(url, 'eth/v1/beacon/headers/head')]);
    for (const url of chain.checkpointz) jobs.push(['checkpointz', url, () => checkBeacon(url, 'eth/v1/beacon/states/head/finality_checkpoints')]);
    for (const url of chain.prover) jobs.push(['prover', url, () => checkProver(url)]);

    const results = await Promise.all(jobs.map(async ([kind, url, fn]) => {
      const r = await fn();
      return { kind, url, ...r };
    }));

    for (const r of results) {
      const mark = r.ok ? 'OK  ' : 'FAIL';
      if (!r.ok) failed++;
      console.log(`  [${mark}] ${r.kind.padEnd(12)} ${r.url}  ${r.detail}`);
    }
  }

  console.log(failed ? `\n${failed} endpoint(s) failed.` : '\nAll probed endpoints responded as expected.');
  process.exit(failed ? 1 : 0);
}

run();
