#!/usr/bin/env node
/**
 * Colibri WASM Mode Benchmark
 *
 * Measures latency and transfer size for all RPC methods across all
 * prover modes (direct, local, remote, hybrid, proxy, lightclient)
 * with and without PAP.
 *
 * Usage:
 *   node test/benchmark.mjs                  # run full benchmark
 *   node test/benchmark.mjs --blocks 2       # fewer blocks (faster)
 *   node test/benchmark.mjs --runs 2         # fewer runs per block
 *   node test/benchmark.mjs --modes remote   # only specific mode(s), comma-separated
 *   node test/benchmark.mjs --debug          # enable Colibri debug logging (shows sub-requests)
 */

import * as fs from 'node:fs';
import { modulePath } from './test_config.js';

const ColibriModule = await import(modulePath);
const Colibri = ColibriModule.default;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

const RPC_URL = 'https://mainnet1.colibri-proof.tech/execution';
const BEACON_URL = 'https://mainnet1.colibri-proof.tech/consensus/';
const PROVER_URL = 'https://mainnet1.colibri-proof.tech/';
const CHAIN_ID = 1;

const args = process.argv.slice(2);
function argVal(name, fallback) {
  const idx = args.indexOf('--' + name);
  return idx >= 0 && args[idx + 1] ? args[idx + 1] : fallback;
}

const NUM_BLOCKS = parseInt(argVal('blocks', '3'), 10);
const RUNS_PER_BLOCK = parseInt(argVal('runs', '5'), 10);
const BLOCK_WAIT_MS = 12_000;
const FILTER_MODES = argVal('modes', '').split(',').filter(Boolean);
const DEBUG = true //args.includes('--debug');

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

async function rpcFetch(method, params) {
  const res = await fetch(RPC_URL, {
    method: 'POST',
    body: JSON.stringify({ jsonrpc: '2.0', id: 1, method, params }),
    headers: { 'Content-Type': 'application/json' },
  });
  const json = await res.json();
  if (json.error) throw new Error(json.error.message || JSON.stringify(json.error));
  return json.result;
}

function fmtKB(bytes) {
  if (bytes == null) return '-';
  return (bytes / 1024).toFixed(1);
}

function fmtMs(ms) {
  if (ms == null) return '-';
  return Math.round(ms);
}

async function rpcFetchWithRetry(method, params, retries = 3) {
  for (let i = 0; i < retries; i++) {
    try {
      return await rpcFetch(method, params);
    } catch (e) {
      if (i === retries - 1) throw e;
      await sleep(2000 * (i + 1));
    }
  }
}

function makeRow(modeDef, block, run, method, latency_ms, transfer_bytes, error) {
  return {
    mode: modeDef.name,
    pap: modeDef.pap ? 'on' : 'off',
    block,
    run,
    method,
    latency_ms,
    transfer_bytes,
    error: error || '',
  };
}

// ---------------------------------------------------------------------------
// Shared constants
// ---------------------------------------------------------------------------

const MODE_NAMES = ['local', 'hybrid', 'lightclient', 'proxy', 'remote', 'direct'];

function modeDisplayName(mode) {
  return mode === 'direct' ? 'unverified' : mode;
}

const METHOD_LABELS = [
  'eth_blockNumber',
  'eth_getBlockByNumber(false)',
  'eth_getBlockByNumber(true)',
  'eth_call',
  'eth_getLogs',
  'eth_getBalance',
  'eth_getTransactionByHash(latest)',
  'eth_getTransactionByHash(historic)',
  'eth_getTransactionReceipt(latest)',
  'eth_getTransactionReceipt(historic)',
];

// ---------------------------------------------------------------------------
// Mode definitions
// ---------------------------------------------------------------------------

const MODE_DEFS = [
  { name: 'direct', pap: false, colibri: false },
  { name: 'local', pap: false, colibri: true, prover_mode: 'local', prover: [] },
  { name: 'local', pap: true, colibri: true, prover_mode: 'local', prover: [] },
  { name: 'remote', pap: false, colibri: true, prover_mode: 'remote', prover: [PROVER_URL] },
  { name: 'remote', pap: true, colibri: true, prover_mode: 'remote', prover: [PROVER_URL] },
  { name: 'proxy', pap: false, colibri: true, prover_mode: 'proxy', prover: [PROVER_URL] },
  { name: 'proxy', pap: true, colibri: true, prover_mode: 'proxy', prover: [PROVER_URL] },
  { name: 'hybrid', pap: false, colibri: true, prover_mode: 'hybrid', prover: [PROVER_URL] },
  { name: 'hybrid', pap: true, colibri: true, prover_mode: 'hybrid', prover: [PROVER_URL] },
  { name: 'lightclient', pap: false, colibri: true, prover_mode: 'light_client', prover: [PROVER_URL] },
  { name: 'lightclient', pap: true, colibri: true, prover_mode: 'light_client', prover: [PROVER_URL] },
];

function modeLabel(m) {
  return m.name + (m.pap ? '+pap' : '');
}

// ---------------------------------------------------------------------------
// Resolve dynamic test data (latest + historical tx hashes)
// ---------------------------------------------------------------------------

async function resolveTestData() {
  console.log('Resolving dynamic test data...');

  const blockNumHex = await rpcFetchWithRetry('eth_blockNumber', []);
  const currentBlock = parseInt(blockNumHex, 16)-2;
  console.log(`  Current block: ${currentBlock} (${blockNumHex})`);

  const latestBlock = await rpcFetchWithRetry('eth_getBlockByNumber', ['0x'+currentBlock.toString(16), true]);
  const latestTxHash = latestBlock.transactions?.[0]?.hash;
  if (!latestTxHash) throw new Error('No transactions in latest block');
  console.log(`  Latest TX: ${latestTxHash}`);

  const historicBlockNum = currentBlock - 432_000;
  const historicHex = '0x' + historicBlockNum.toString(16);
  const historicBlock = await rpcFetchWithRetry('eth_getBlockByNumber', [historicHex, true]);
  const historicTxHash = historicBlock.transactions?.[0]?.hash;
  if (!historicTxHash) throw new Error('No transactions in historic block');
  console.log(`  Historic TX: ${historicTxHash} (block ${historicBlockNum})`);

  const logsBlockHex = '0x' + currentBlock.toString(16);

  return { latestTxHash, historicTxHash, logsBlockHex };
}

// ---------------------------------------------------------------------------
// Build RPC request set
// ---------------------------------------------------------------------------

function buildRequests(testData) {
  return [
    {
      label: 'eth_blockNumber',
      method: 'eth_blockNumber',
      params: [],
    },
    {
      label: 'eth_getBlockByNumber(false)',
      method: 'eth_getBlockByNumber',
      params: ['latest', false],
    },
    {
      label: 'eth_getBlockByNumber(true)',
      method: 'eth_getBlockByNumber',
      params: ['latest', true],
    },
    {
      label: 'eth_call',
      method: 'eth_call',
      params: [
        {
          to: '0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48',
          data: '0x70a0823100000000000000000000000037305b1cd40574e4c5ce33f8e8306be057fd7341',
        },
        'latest',
      ],
    },
    {
      label: 'eth_getLogs',
      method: 'eth_getLogs',
      params: [
        {
          address: ['0xdac17f958d2ee523a2206206994597c13d831ec7'],
          fromBlock: testData.logsBlockHex,
          toBlock: testData.logsBlockHex,
        },
      ],
    },
    {
      label: 'eth_getBalance',
      method: 'eth_getBalance',
      params: ['0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5', 'latest'],
    },
    {
      label: 'eth_getTransactionByHash(latest)',
      method: 'eth_getTransactionByHash',
      params: [testData.latestTxHash],
    },
    {
      label: 'eth_getTransactionByHash(historic)',
      method: 'eth_getTransactionByHash',
      params: [testData.historicTxHash],
      historic: true,
    },
    {
      label: 'eth_getTransactionReceipt(latest)',
      method: 'eth_getTransactionReceipt',
      params: [testData.latestTxHash],
    },
    {
      label: 'eth_getTransactionReceipt(historic)',
      method: 'eth_getTransactionReceipt',
      params: [testData.historicTxHash],
      historic: true,
    },
  ];
}

// ---------------------------------------------------------------------------
// Execute a single RPC call and measure it
// ---------------------------------------------------------------------------

async function measureDirect(method, params) {
  const bodyStr = JSON.stringify({ jsonrpc: '2.0', id: 1, method, params });

  const t0 = performance.now();
  const res = await fetch(RPC_URL, {
    method: 'POST',
    body: bodyStr,
    headers: { 'Content-Type': 'application/json' },
  });
  const blob = await res.blob();
  const elapsed = performance.now() - t0;

  return { latency_ms: elapsed, transfer_bytes: blob.size, error: null };
}

async function measureColibri(c4, method, params, modeLabel) {
  let transferBytes = 0;
  c4.config.onTransfer = (size) => { transferBytes += size; };

  const t0 = performance.now();
  try {
    await c4.rpc(method, params);
    const elapsed = performance.now() - t0;
    return { latency_ms: elapsed, transfer_bytes: transferBytes, error: null };
  } catch (e) {
    const elapsed = performance.now() - t0;
    console.error(`\n  *** ERROR in [${modeLabel}] ${method}(${JSON.stringify(params)})`);
    console.error(`      Config: prover_mode=${c4.config.prover_mode}, privacy_mode=${c4.config.privacy_mode}, prover=${JSON.stringify(c4.config.prover)}`);
    console.error(`      Config:`, c4.config);
    console.error(`      RPC:`, JSON.stringify({ method, params },null,2));
    console.error(`      Error:  ${e.message}\n`);
    return { latency_ms: elapsed, transfer_bytes: transferBytes, error: e.message };
  } finally {
    c4.config.onTransfer = undefined;
  }
}

// ---------------------------------------------------------------------------
// Run benchmark for one mode configuration
// ---------------------------------------------------------------------------

async function benchmarkMode(modeDef, requests) {
  const label = modeLabel(modeDef);
  const rows = [];

  for (let block = 0; block < NUM_BLOCKS; block++) {
    // Fresh storage per block
    const storage = createMemoryStorage();
    Colibri.register_storage(storage);

    let c4 = null;
    if (modeDef.colibri) {
      c4 = new Colibri({
        chainId: CHAIN_ID,
        prover_mode: modeDef.prover_mode,
        prover: modeDef.prover,
        rpcs: [RPC_URL],
        beacon_apis: [BEACON_URL],
        privacy_mode: modeDef.pap ? 'basic' : 'none',
        zk_proof: true,
        debug: DEBUG,
      });
    }

    // Sync: first eth_blockNumber with cold cache
    let syncFailed = false;
    if (c4) {
      let syncTransfer = 0;
      c4.config.onTransfer = (size) => { syncTransfer += size; };
      const t0 = performance.now();
      try {
        await c4.rpc('eth_blockNumber', []);
        const syncTime = performance.now() - t0;
        rows.push(makeRow(modeDef, block + 1, 0, '__sync__', syncTime, syncTransfer, ''));
        process.stdout.write(`  [${label}] block ${block + 1} sync: ${Math.round(syncTime)}ms\n`);
      } catch (e) {
        syncFailed = true;
        const syncTime = performance.now() - t0;
        rows.push(makeRow(modeDef, block + 1, 0, '__sync__', syncTime, syncTransfer, e.message));
        console.error(`\n  *** SYNC ERROR in [${label}] eth_blockNumber([])`);
        console.error(`      Config: prover_mode=${c4.config.prover_mode}, privacy_mode=${c4.config.privacy_mode}, prover=${JSON.stringify(c4.config.prover)}`);
        console.error(`      Error:  ${e.message}\n`);
        process.stdout.write(`  [${label}] block ${block + 1} sync FAILED, skipping runs\n`);
      } finally {
        c4.config.onTransfer = undefined;
      }
    }

    if (syncFailed) {
      if (block < NUM_BLOCKS - 1) await sleep(BLOCK_WAIT_MS);
      continue;
    }

    for (let run = 0; run < RUNS_PER_BLOCK; run++) {
      // lightclient warmup before each run
      if (modeDef.prover_mode === 'light_client' && c4) {
        try {
          await c4.rpc('eth_getBlockHeader', ['latest']);
        } catch { /* best effort */ }
      }

      for (const req of requests) {
        // local mode cannot do historic proofs
        if (req.historic && modeDef.prover_mode === 'local') {
          rows.push(makeRow(modeDef, block + 1, run + 1, req.label, null, null, 'skipped (local+historic)'));
          continue;
        }

        let result;
        if (!modeDef.colibri) {
          result = await measureDirect(req.method, req.params);
        } else {
          result = await measureColibri(c4, req.method, req.params, label);
        }

        rows.push(makeRow(modeDef, block + 1, run + 1, req.label, result.latency_ms, result.transfer_bytes, result.error));

        const status = result.error ? `ERR: ${result.error.slice(0, 120)}` : `${fmtMs(result.latency_ms)}ms ${fmtKB(result.transfer_bytes)}kB`;
        process.stdout.write(`  [${label}] b${block + 1}r${run + 1} ${req.label}: ${status}\n`);
      }
    }

    if (block < NUM_BLOCKS - 1) {
      process.stdout.write(`  Waiting ${BLOCK_WAIT_MS / 1000}s for next block...\n`);
      await sleep(BLOCK_WAIT_MS);
    }
  }

  return rows;
}

// ---------------------------------------------------------------------------
// Aggregation helpers
// ---------------------------------------------------------------------------

function aggregate(allRows) {
  const byModeMethod = {};

  for (const row of allRows) {
    if (row.method === '__sync__') continue;
    if (row.latency_ms == null) continue;
    const key = `${row.mode}|${row.pap}|${row.method}`;
    if (!byModeMethod[key]) byModeMethod[key] = { ...row, latencies: [], bytes: [] };
    byModeMethod[key].latencies.push(row.latency_ms);
    byModeMethod[key].bytes.push(row.transfer_bytes);
  }

  const result = {};
  for (const [key, val] of Object.entries(byModeMethod)) {
    const lats = val.latencies.sort((a, b) => a - b);
    const bts = val.bytes;
    result[key] = {
      mode: val.mode,
      pap: val.pap,
      method: val.method,
      min_ms: lats[0],
      max_ms: lats[lats.length - 1],
      avg_ms: lats.reduce((a, b) => a + b, 0) / lats.length,
      avg_bytes: bts.reduce((a, b) => a + b, 0) / bts.length,
      count: lats.length,
    };
  }
  return result;
}

function aggregateSync(allRows) {
  const byMode = {};
  for (const row of allRows) {
    if (row.method !== '__sync__') continue;
    const key = `${row.mode}|${row.pap}`;
    if (!byMode[key]) byMode[key] = { mode: row.mode, pap: row.pap, times: [] };
    if (row.latency_ms != null) byMode[key].times.push(row.latency_ms);
  }
  for (const val of Object.values(byMode)) {
    val.avg_ms = val.times.length ? val.times.reduce((a, b) => a + b, 0) / val.times.length : null;
  }
  return byMode;
}

// ---------------------------------------------------------------------------
// Output: CSV
// ---------------------------------------------------------------------------

function writeCSV(allRows, filename) {
  const header = 'mode,pap,block,run,method,latency_ms,transfer_bytes,error';
  const lines = allRows.map((r) =>
    [r.mode, r.pap, r.block, r.run, r.method,
      r.latency_ms != null ? r.latency_ms.toFixed(2) : '',
      r.transfer_bytes != null ? r.transfer_bytes : '',
      r.error.replace(/,/g, ';'),
    ].join(',')
  );
  fs.writeFileSync(filename, [header, ...lines].join('\n') + '\n');
  console.log(`\nCSV written to ${filename}`);
}

// ---------------------------------------------------------------------------
// Output: Markdown (gitbook-ready)
// ---------------------------------------------------------------------------

function writeMarkdown(agg, syncAgg, filename) {
  const modes = MODE_NAMES;
  const methodLabels = METHOD_LABELS;

  function lookup(mode, pap, method) {
    return agg[`${mode}|${pap}|${method}`];
  }

  let md = '';

  md += '## End-to-End Performance\n\n';
  md += 'Measured using the WASM client against `mainnet1.colibri-proof.tech`.  \n';
  md += `${NUM_BLOCKS} blocks, ${RUNS_PER_BLOCK} runs per block. Best-case (min) latency shown.\n\n`;

  const displayNames = modes.map(modeDisplayName);

  // --- Latency table (no-PAP only for compactness) ---
  md += '### Latency (best-case, ms)\n\n';
  md += `| Method | ${displayNames.join(' | ')} |\n`;
  md += `|--------|${modes.map(() => '------').join('|')}|\n`;

  for (const method of methodLabels) {
    const cells = modes.map((mode) => {
      const entry = lookup(mode, 'off', method);
      if (!entry) return '-';
      return fmtMs(mode == 'direct' ? entry.avg_ms : entry.min_ms);
    });
    md += `| ${method} | ${cells.join(' | ')} |\n`;
  }

  // --- Transfer size table ---
  md += '\n### Transfer Size (avg, kB)\n\n';
  md += `| Method | ${displayNames.join(' | ')} |\n`;
  md += `|--------|${modes.map(() => '------').join('|')}|\n`;

  for (const method of methodLabels) {
    const cells = modes.map((mode) => {
      const entry = lookup(mode, 'off', method);
      if (!entry) return '-';
      return fmtKB(entry.avg_bytes);
    });
    md += `| ${method} | ${cells.join(' | ')} |\n`;
  }

  // --- Sync time (single best value across all modes) ---
  {
    const realSyncTimes = [];
    for (const mode of modes) {
      if (mode === 'direct') continue;
      for (const pap of ['off', 'on']) {
        const entry = syncAgg[`${mode}|${pap}`];
        if (entry?.avg_ms != null && entry.avg_ms > 100) realSyncTimes.push(entry.avg_ms);
      }
    }
    const bestSync = realSyncTimes.length ? Math.min(...realSyncTimes) : null;
    md += `\n### Sync Time\n\nInitial sync (cold cache): **${bestSync != null ? fmtMs(bestSync) + ' ms' : '-'}**\n`;
  }

  // --- PAP impact (best / worst overhead) ---
  const verifiedModes = modes.filter((m) => m !== 'direct');
  const verifiedDisplayNames = verifiedModes.map(modeDisplayName);
  md += '\n### PAP Impact on Latency (best / worst overhead, ms)\n\n';
  md += `| Method | ${verifiedDisplayNames.join(' | ')} |\n`;
  md += `|--------|${verifiedModes.map(() => '------').join('|')}|\n`;

  for (const method of methodLabels) {
    const cells = verifiedModes.map((mode) => {
      const noPap = lookup(mode, 'off', method);
      const pap = lookup(mode, 'on', method);
      if (!noPap || !pap) return '-';
      const best = pap.min_ms - noPap.min_ms;
      const worst = pap.max_ms - noPap.min_ms;
      const fmtDiff = (d) => (d >= 0 ? '+' : '') + fmtMs(d);

      return method == 'eth_call' ? `${fmtDiff(best)} / ${fmtDiff(worst)}` : `${fmtDiff(best)}`;
    });
    md += `| ${method} | ${cells.join(' | ')} |\n`;
  }

  md += '\n';
  fs.writeFileSync(filename, md);
  console.log(`Markdown written to ${filename}`);
}

// ---------------------------------------------------------------------------
// Console summary
// ---------------------------------------------------------------------------

function printSummary(agg, syncAgg) {
  const modes = MODE_NAMES;
  const methodLabels = METHOD_LABELS;

  console.log('\n' + '='.repeat(80));
  console.log('BENCHMARK SUMMARY (best-case latency, ms)');
  console.log('='.repeat(80));

  const colW = 14;
  const methW = 40;

  const header = 'Method'.padEnd(methW) + modes.map((m) => modeDisplayName(m).padStart(colW)).join('');
  console.log(header);
  console.log('-'.repeat(header.length));

  for (const method of methodLabels) {
    const cells = modes.map((mode) => {
      const entry = agg[`${mode}|off|${method}`];
      if (!entry) return '-'.padStart(colW);
      return (fmtMs(entry.min_ms) + 'ms').padStart(colW);
    });
    console.log(method.padEnd(methW) + cells.join(''));
  }

  console.log('\n' + '-'.repeat(60));
  console.log('SYNC TIME');
  console.log('-'.repeat(60));
  const realSyncTimes = [];
  for (const mode of modes) {
    if (mode === 'direct') continue;
    for (const pap of ['off', 'on']) {
      const entry = syncAgg[`${mode}|${pap}`];
      if (entry?.avg_ms != null && entry.avg_ms > 100) realSyncTimes.push(entry.avg_ms);
    }
  }
  const bestSync = realSyncTimes.length ? Math.min(...realSyncTimes) : null;
  console.log(`  Initial sync (cold cache): ${bestSync != null ? fmtMs(bestSync) + 'ms' : '-'}`);
  console.log('');
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

async function main() {
  console.log(`Colibri WASM Benchmark`);
  console.log(`  Blocks: ${NUM_BLOCKS}, Runs/block: ${RUNS_PER_BLOCK}`);
  console.log(`  RPC: ${RPC_URL}`);
  console.log(`  Prover: ${PROVER_URL}`);
  console.log('');

  const testData = await resolveTestData();
  const requests = buildRequests(testData);

  let activeModes = MODE_DEFS;
  if (FILTER_MODES.length > 0) {
    activeModes = MODE_DEFS.filter((m) => FILTER_MODES.includes(m.name) || FILTER_MODES.includes(modeLabel(m)));
  }

  console.log(`\nModes to test: ${activeModes.map(modeLabel).join(', ')}\n`);

  const allRows = [];

  for (const modeDef of activeModes) {
    const label = modeLabel(modeDef);
    console.log(`\n${'='.repeat(60)}`);
    console.log(`Running: ${label}`);
    console.log('='.repeat(60));

    const rows = await benchmarkMode(modeDef, requests);
    allRows.push(...rows);
  }

  // Write outputs
  const csvFile = 'benchmark-results.csv';
  const mdFile = 'benchmark-results.md';

  writeCSV(allRows, csvFile);

  const agg = aggregate(allRows);
  const syncAgg = aggregateSync(allRows);

  writeMarkdown(agg, syncAgg, mdFile);
  printSummary(agg, syncAgg);

  console.log('\nBenchmark complete.');
}

main().catch((e) => {
  console.error('Benchmark failed:', e);
  process.exit(1);
});
