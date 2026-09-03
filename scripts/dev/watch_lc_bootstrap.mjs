#!/usr/bin/env node
/**
 * Poll Beacon light-client bootstrap availability.
 *
 * Every `--interval` ms (default 12s) each URL:
 *   1. GET /eth/v1/beacon/states/head/finality_checkpoints
 *   2. GET /eth/v1/beacon/headers/head          (slot, for epoch-offset)
 *   3. GET /eth/v1/beacon/light_client/bootstrap/{data.finalized.root}
 *      with Accept: application/octet-stream
 *
 * Default URLs are plataberget's beacon_api entries in chains.json.
 *
 * Usage:
 *   node scripts/dev/watch_lc_bootstrap.mjs
 *   node scripts/dev/watch_lc_bootstrap.mjs --interval 12 --out /tmp/lc.jsonl
 *   node scripts/dev/watch_lc_bootstrap.mjs --url http://localhost:5052
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const SPEC = JSON.parse(fs.readFileSync(path.join(ROOT, 'scripts/chain_defaults/chains.json'), 'utf8'));
const SLOTS_PER_EPOCH = 32;
const TIMEOUT_MS = 15_000;

function argVal(name, fallback) {
  const idx = process.argv.indexOf('--' + name);
  if (idx >= 0 && process.argv[idx + 1] && !process.argv[idx + 1].startsWith('--')) return process.argv[idx + 1];
  return fallback;
}

function collectUrls() {
  const urls = [];
  for (let i = 0; i < process.argv.length; i++) {
    if (process.argv[i] === '--url' && process.argv[i + 1]) urls.push(process.argv[++i]);
  }
  if (urls.length) return urls;
  const plataberget = SPEC.chains.find((c) => c.name === 'plataberget');
  if (!plataberget?.beacon_api?.length) throw new Error('chains.json: plataberget.beacon_api missing');
  return plataberget.beacon_api;
}

const INTERVAL_MS = Math.max(1000, Number(argVal('interval', '12')) * 1000);
const OUT_PATH = argVal('out', '');
const URLS = collectUrls();
const USE_COLOR = process.stdout.isTTY && !process.env.NO_COLOR;

const GREEN = USE_COLOR ? '\x1b[32m' : '';
const RED = USE_COLOR ? '\x1b[31m' : '';
const RESET = USE_COLOR ? '\x1b[0m' : '';

function paint(ok, text) {
  return `${ok ? GREEN : RED}${text}${RESET}`;
}

function labelOf(url) {
  try {
    return new URL(url).hostname.replace(/^www\./, '');
  } catch {
    return url;
  }
}

function joinUrl(base, suffix) {
  return `${String(base).replace(/\/+$/, '')}/${String(suffix).replace(/^\/+/, '')}`;
}

function padRoot(root) {
  const hex = String(root || '').replace(/^0x/i, '').toLowerCase();
  return hex ? `0x${hex}` : null;
}

function shortRoot(root) {
  const r = padRoot(root);
  return r ? `${r.slice(0, 10)}…${r.slice(-6)}` : '-';
}

function nowIso() {
  return new Date().toISOString();
}

function clock() {
  return new Date().toISOString().slice(11, 23);
}

async function fetchOnce(url, { accept, asJson } = {}) {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), TIMEOUT_MS);
  const t0 = Date.now();
  try {
    const res = await fetch(url, {
      signal: ctrl.signal,
      redirect: 'follow',
      headers: accept ? { Accept: accept } : { Accept: 'application/json' },
    });
    const buf = Buffer.from(await res.arrayBuffer());
    let json = null;
    let errMsg = '';
    if (asJson || (res.headers.get('content-type') || '').includes('json')) {
      try {
        json = JSON.parse(buf.toString('utf8'));
      } catch {
        /* keep raw */
      }
    }
    if (!res.ok) {
      errMsg = json?.message || json?.error || buf.toString('utf8').replace(/\s+/g, ' ').slice(0, 160);
    }
    return { ok: res.ok, status: res.status, ms: Date.now() - t0, bytes: buf.length, json, errMsg };
  } catch (e) {
    return {
      ok: false,
      status: 0,
      ms: Date.now() - t0,
      bytes: 0,
      json: null,
      errMsg: e instanceof Error ? e.message : String(e),
    };
  } finally {
    clearTimeout(timer);
  }
}

async function probe(base) {
  const cp = await fetchOnce(joinUrl(base, 'eth/v1/beacon/states/head/finality_checkpoints'), { asJson: true });
  const head = await fetchOnce(joinUrl(base, 'eth/v1/beacon/headers/head'), { asJson: true });

  const finalized = cp.json?.data?.finalized || {};
  const epoch = finalized.epoch != null ? Number(finalized.epoch) : null;
  const root = padRoot(finalized.root);
  const slotRaw = head.json?.data?.header?.message?.slot;
  const slot = slotRaw != null ? Number(slotRaw) : null;
  const slotInEpoch = Number.isFinite(slot) ? slot % SLOTS_PER_EPOCH : null;
  const slotsAfterFin = Number.isFinite(slot) && Number.isFinite(epoch)
    ? slot - epoch * SLOTS_PER_EPOCH
    : null;

  let bootstrap = null;
  if (root) {
    bootstrap = await fetchOnce(
      joinUrl(base, `eth/v1/beacon/light_client/bootstrap/${root}`),
      { accept: 'application/octet-stream' },
    );
  } else {
    bootstrap = {
      ok: false,
      status: 0,
      ms: 0,
      bytes: 0,
      json: null,
      errMsg: cp.ok ? 'missing data.finalized.root' : (cp.errMsg || `checkpoints HTTP ${cp.status}`),
    };
  }

  return {
    ts: nowIso(),
    url: base,
    host: labelOf(base),
    epoch,
    root,
    slot,
    slotInEpoch,
    slotsAfterFin,
    checkpoints: { ok: cp.ok, status: cp.status, ms: cp.ms, errMsg: cp.errMsg },
    head: { ok: head.ok, status: head.status, ms: head.ms },
    bootstrap: {
      ok: bootstrap.ok,
      status: bootstrap.status,
      ms: bootstrap.ms,
      bytes: bootstrap.bytes,
      errMsg: bootstrap.errMsg,
    },
  };
}

function fmtProbe(p) {
  const ep = p.epoch != null ? String(p.epoch) : '?';
  const sl = p.slot != null ? String(p.slot) : '?';
  const off = p.slotsAfterFin != null ? String(p.slotsAfterFin) : '?';
  const inEp = p.slotInEpoch != null ? String(p.slotInEpoch).padStart(2, '0') : '??';
  const b = p.bootstrap;
  const mark = b.ok ? 'OK ' : 'ERR';
  const extra = b.ok ? `${b.bytes}B` : (b.errMsg || `HTTP ${b.status}`);
  const status = paint(b.ok, `${mark} ${String(b.status).padStart(3)} ${String(b.ms).padStart(4)}ms  ${extra}`);
  return [
    clock(),
    p.host.padEnd(36),
    `ep=${ep}`.padEnd(8),
    `slot=${sl}`.padEnd(14),
    `fin+${off}`.padEnd(9),
    `inEp=${inEp}`,
    shortRoot(p.root).padEnd(20),
    status,
  ].join('  ');
}

const firstOk = new Map();
const lastRoot = new Map();
const stats = new Map();

function ensureStats(host) {
  if (!stats.has(host)) stats.set(host, { polls: 0, ok: 0, fail: 0 });
  return stats.get(host);
}

function note(p) {
  const st = ensureStats(p.host);
  st.polls += 1;
  if (p.bootstrap.ok) st.ok += 1;
  else st.fail += 1;

  const prev = lastRoot.get(p.host);
  if (p.root && p.root !== prev) {
    lastRoot.set(p.host, p.root);
    firstOk.delete(p.host);
    console.log(`\n--- ${p.host}: new finalized ${shortRoot(p.root)} epoch=${p.epoch} ---\n`);
  }

  if (p.bootstrap.ok && !firstOk.has(p.host)) {
    firstOk.set(p.host, p);
    const off = p.slotsAfterFin != null ? `${p.slotsAfterFin} slots after finalized-epoch start` : 'slot unknown';
    const inEp = p.slotInEpoch != null ? `, slot-in-epoch ${p.slotInEpoch}/${SLOTS_PER_EPOCH}` : '';
    console.log(paint(true, `>>> FIRST OK  ${p.host}  ${off}${inEp}`) + '\n');
  }
}

function summary() {
  console.log('\n=== summary ===');
  for (const url of URLS) {
    const host = labelOf(url);
    const st = stats.get(host) || { polls: 0, ok: 0, fail: 0 };
    const first = firstOk.get(host);
    const firstInfo = first
      ? paint(true, `first OK at slot=${first.slot} fin+${first.slotsAfterFin} inEp=${first.slotInEpoch} ${shortRoot(first.root)}`)
      : paint(false, 'never OK');
    console.log(`  ${host}: ${st.ok}/${st.polls} ok  (${st.fail} fail)  ${firstInfo}`);
  }
}

function writeOut(row) {
  if (!OUT_PATH) return;
  fs.appendFileSync(OUT_PATH, JSON.stringify(row) + '\n');
}

console.log(`Watching light_client/bootstrap every ${INTERVAL_MS / 1000}s`);
console.log(`Accept: application/octet-stream`);
for (const u of URLS) console.log(`  ${labelOf(u)}  ${u}`);
if (OUT_PATH) console.log(`JSONL: ${OUT_PATH}`);
console.log('Ctrl-C to stop.\n');

process.on('SIGINT', () => {
  summary();
  process.exit(0);
});
process.on('SIGTERM', () => {
  summary();
  process.exit(0);
});

async function tick() {
  const rows = await Promise.all(URLS.map((u) => probe(u)));
  for (const p of rows) {
    note(p);
    writeOut(p);
    console.log(fmtProbe(p));
  }
}

await tick();
setInterval(() => {
  tick().catch((e) => console.error('tick failed:', e));
}, INTERVAL_MS);
