#!/usr/bin/env node

// Simple CORS checker for checkpoint-related endpoints from the shared chain defaults.
// - Reads scripts/chain_defaults/chains.json
// - Sends a preflight OPTIONS + GET with an Origin header
// - Reports whether Access-Control-Allow-Origin is present/acceptable

import fs from 'fs/promises';
import { fileURLToPath } from 'url';
import path from 'path';

const CHAINS_JSON = path.resolve(fileURLToPath(new URL('.', import.meta.url)), '../../../scripts/chain_defaults/chains.json');
const ORIGIN = 'https://example.com';
const ENDPOINT_SUFFIX = 'eth/v1/beacon/states/head/finality_checkpoints';
const TIMEOUT_MS = 8000;

function timeoutSignal(ms) {
  const ctrl = new AbortController();
  const id = setTimeout(() => ctrl.abort(new Error('timeout')), ms);
  return { signal: ctrl.signal, cancel: () => clearTimeout(id) };
}

function parseUrlsByKind(spec) {
  const out = { checkpointz: new Set(), beacon_apis: new Set(), prover: new Set() };
  for (const chain of spec.chains || []) {
    for (const url of chain.checkpointz || []) out.checkpointz.add(url);
    for (const url of chain.beacon_api || []) out.beacon_apis.add(url);
    for (const url of chain.prover || []) out.prover.add(url);
  }
  return {
    checkpointz: Array.from(out.checkpointz),
    beacon_apis: Array.from(out.beacon_apis),
    prover: Array.from(out.prover),
  };
}

async function checkCors(urlBase) {
  const base = urlBase.endsWith('/') ? urlBase : urlBase + '/';
  const url = base + ENDPOINT_SUFFIX;
  const res = { url, preflight: { ok: false, aco: null, acm: null }, get: { ok: false, aco: null }, note: '' };

  // Preflight
  try {
    const t = timeoutSignal(TIMEOUT_MS);
    const r = await fetch(url, {
      method: 'OPTIONS',
      headers: {
        'Origin': ORIGIN,
        'Access-Control-Request-Method': 'GET',
        'Access-Control-Request-Headers': 'content-type',
      },
      signal: t.signal,
    });
    t.cancel();
    const aco = r.headers.get('access-control-allow-origin');
    const acm = r.headers.get('access-control-allow-methods');
    res.preflight.aco = aco;
    res.preflight.acm = acm;
    res.preflight.ok = !!aco && (aco === '*' || aco === ORIGIN);
  } catch (e) {
    res.note = 'preflight failed: ' + (e?.message || String(e));
  }

  // GET
  try {
    const t = timeoutSignal(TIMEOUT_MS);
    const r = await fetch(url, {
      method: 'GET',
      headers: {
        'Origin': ORIGIN,
        'Content-Type': 'application/json',
      },
      signal: t.signal,
    });
    t.cancel();
    const aco = r.headers.get('access-control-allow-origin');
    res.get.aco = aco;
    res.get.ok = r.ok && !!aco && (aco === '*' || aco === ORIGIN);
    if (!r.ok && !res.note) res.note = `GET status ${r.status}`;
  } catch (e) {
    res.note = (res.note ? res.note + '; ' : '') + 'get failed: ' + (e?.message || String(e));
  }

  return res;
}

function printReport(results) {
  const pad = (s, n) => (s + '').padEnd(n);
  console.log(pad('KIND', 12), pad('URL', 70), pad('PREFLIGHT', 12), pad('GET', 8), 'ALLOW-ORIGIN');
  console.log('-'.repeat(125));
  for (const r of results) {
    const okP = r.preflight.ok ? 'OK' : 'FAIL';
    const okG = r.get.ok ? 'OK' : 'FAIL';
    console.log(pad(r.kind, 12), pad(r.url, 70), pad(okP, 12), pad(okG, 8), r.get.aco || r.preflight.aco || '-');
    if (r.note) console.log('  note:', r.note);
  }
}

async function main() {
  const spec = JSON.parse(await fs.readFile(CHAINS_JSON, 'utf8'));
  const byKind = parseUrlsByKind(spec);
  const results = [];
  for (const kind of ['checkpointz', 'beacon_apis', 'prover']) {
    for (const url of byKind[kind]) {
      const r = await checkCors(url);
      results.push({ kind, ...r });
    }
  }
  if (!results.length) {
    console.error('No URLs found in chains.json');
    process.exit(1);
  }
  printReport(results);
  const problematic = results.filter(r => !(r.preflight.ok && r.get.ok));
  process.exit(problematic.length ? 2 : 0);
}

main().catch(e => {
  console.error(e);
  process.exit(1);
});


