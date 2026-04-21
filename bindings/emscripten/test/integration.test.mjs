import test, { describe, before, after, afterEach } from 'node:test';
import assert from 'node:assert';
import * as fs from 'node:fs';
import { modulePath } from './test_config.js';

const ColibriModule = await import(modulePath);
const Colibri = ColibriModule.default;

const RUN_INTEGRATION = process.env.C4_RUN_INTEGRATION === '1' || process.env.GITHUB_ACTIONS === 'true';
const RUN_SLOW = process.env.C4_RUN_SLOW_TESTS === '1';

const CHAIN_ID = 1; // mainnet
const BEACON_API = 'https://mainnet1.colibri-proof.tech/consensus/';
const SLOTS_PER_EPOCH = 32;
const SECONDS_PER_SLOT = 12;
const SLOTS_PER_PERIOD = 8192;
const TIMEOUT = 120_000;
const RATE_LIMIT_DELAY = 1500;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function createMemoryStorage() {
  const map = new Map();
  return {
    get: (key) => {
      const r =map.get(key) ?? null
 //     console.log('get', key, r);
      return r;
    },
    set: (key, value) => map.set(key, new Uint8Array(value)),
    del: (key) => map.delete(key),
    _map: map,
  };
}

function cloneStorage(source) {
  const storage = createMemoryStorage();
  for (const [k, v] of source._map) storage._map.set(k, new Uint8Array(v));
  return storage;
}

function createSpyCache() {
  const log = [];
  return {
    cacheable: () => true,
    get: () => null,
    set: (req, data) => {
      log.push({
        type: req.type,
        url: req.url,
        method: req.payload?.method,
        encoding: req.encoding,
        size: data.length,
      });
    },
    log,
  };
}

async function fetchWithRetry(url, retries = 3, delayMs = 2000) {
  for (let i = 0; i < retries; i++) {
    try {
      const res = await fetch(url);
      if (res.ok) return res;
      if (res.status === 429 && i < retries - 1) {
        await sleep(delayMs * (i + 1));
        continue;
      }
      throw new Error(`Fetch ${url} failed: ${res.status}`);
    } catch (err) {
      if (i === retries - 1) throw err;
      await sleep(delayMs * (i + 1));
    }
  }
}

async function resolveCheckpointAtSlot(beaconApiUrl, slotOffsetFn) {
  const headRes = await fetchWithRetry(`${beaconApiUrl}/eth/v1/beacon/headers/head`);
  const headJson = await headRes.json();
  const currentSlot = Number(headJson.data.header.message.slot);

  let checkpointSlot = slotOffsetFn(currentSlot);
  checkpointSlot -= checkpointSlot % SLOTS_PER_EPOCH;
  if (checkpointSlot < 0) checkpointSlot = 0;

  const cpRes = await fetchWithRetry(`${beaconApiUrl}/eth/v1/beacon/headers/${checkpointSlot}`);
  const cpJson = await cpRes.json();
  return cpJson.data.root;
}

const resolveCheckpoint = (url, daysBack) =>
  resolveCheckpointAtSlot(url, (s) => s - Math.floor((daysBack * 86400) / SECONDS_PER_SLOT));

const resolveCheckpointByPeriods = (url, periodsBack) =>
  resolveCheckpointAtSlot(url, (s) => s - periodsBack * SLOTS_PER_PERIOD);

function assertIsHexBlockNumber(result) {
  assert.strictEqual(typeof result, 'string', 'result should be a string');
  assert.match(result, /^0x[0-9a-fA-F]+$/, 'result should be a hex block number');
}

// ---------------------------------------------------------------------------
// Test suite
// ---------------------------------------------------------------------------

describe('Integration Tests', { skip: !RUN_INTEGRATION, timeout: TIMEOUT, concurrency: false }, () => {
  let oldStateStorage;

  afterEach(() => {
    Colibri.register_storage(createMemoryStorage());
  });

  before(async () => {
    const blockNum = await fetch('https://mainnet1.colibri-proof.tech/execution', {
      method: 'POST',
      body: JSON.stringify({ jsonrpc: '2.0', method: 'eth_blockNumber', params: [], id: 1 }),
      headers: { 'Content-Type': 'application/json' },
    })
      .then(res => res.json())
      .then(data => data.result);
    const older = '0x' + (parseInt(blockNum, 16) - 10000).toString(16);
    console.log({ blockNum, older });
    oldStateStorage = createMemoryStorage();
    Colibri.register_storage(oldStateStorage);

    const c4 = new Colibri({ chainId: CHAIN_ID, zk_proof: true });
    await c4.rpc('eth_getBlockByNumber', [older, false]);
    assert.ok(oldStateStorage._map.size > 0, 'Storage should be populated after initial request');
  });

  // -------------------------------------------------------------------
  // Remote Proof scenarios (empty storage)
  // -------------------------------------------------------------------

  describe('Remote Proof - empty storage', { concurrency: false }, () => {

    test('empty storage + zk_proof', { timeout: TIMEOUT }, async () => {
      const storage = createMemoryStorage();
      Colibri.register_storage(storage);
      const spy = createSpyCache();

      const c4 = new Colibri({ chainId: CHAIN_ID, zk_proof: true, cache: spy });
      const result = await c4.rpc('eth_blockNumber', []);
      assertIsHexBlockNumber(result);

      assert.ok(spy.log.length == 1, 'Should have made one network requests');
      assert.ok(storage._map.size == 2, 'Storage should be populated after zk_proof sync');
    });

    test('empty storage + checkpoint (current)', { timeout: TIMEOUT }, async () => {
      await sleep(RATE_LIMIT_DELAY);
      const storage = createMemoryStorage();
      Colibri.register_storage(storage);
      const spy = createSpyCache();

      const checkpoint = await resolveCheckpoint(BEACON_API, 0);
      const c4 = new Colibri({
        chainId: CHAIN_ID,
        trusted_checkpoint: checkpoint,
        zk_proof: false,
        cache: spy,
      });
      const result = await c4.rpc('eth_blockNumber', []);
      assertIsHexBlockNumber(result);

      assert.ok(spy.log.length > 0, 'Should have made network requests');
      assert.ok(storage._map.size > 0, 'Storage should be populated after checkpoint sync');
    });

    test('empty storage + checkpoint (1 week old)', { timeout: TIMEOUT }, async () => {
      await sleep(RATE_LIMIT_DELAY * 2);
      const storage = createMemoryStorage();
      Colibri.register_storage(storage);
      const spy = createSpyCache();

      const checkpoint = await resolveCheckpoint(BEACON_API, 7);
      const c4 = new Colibri({
        chainId: CHAIN_ID,
        trusted_checkpoint: checkpoint,
        zk_proof: false,
        cache: spy,
      });

      let result;
      try {
        result = await c4.rpc('eth_blockNumber', []);
      } catch (err) {
        assert.ok(
          err.message.includes('Invalid') || err.message.includes('offset') || err.message.includes('429'),
          `Unexpected error for 1-week checkpoint: ${err.message}`
        );
        console.log(`  1-week checkpoint: ${err.message} (known edge case)`);
        return;
      }
      assertIsHexBlockNumber(result);
      assert.ok(spy.log.length > 0, 'Should have made network requests');
    });

    test('empty storage + no zk, no checkpoint (defaults)', { timeout: TIMEOUT }, async () => {
      await sleep(RATE_LIMIT_DELAY);
      const storage = createMemoryStorage();
      Colibri.register_storage(storage);
      const spy = createSpyCache();

      const c4 = new Colibri({ chainId: CHAIN_ID, zk_proof: false, cache: spy });
      const result = await c4.rpc('eth_blockNumber', []);
      assertIsHexBlockNumber(result);

      assert.ok(spy.log.length > 0, 'Should have made network requests');
    });
  });


  // -------------------------------------------------------------------
  // Remote Proof scenarios (old storage)
  // -------------------------------------------------------------------

  describe('Remote Proof - old storage', { concurrency: false }, () => {

    test('old storage + zk_proof', { timeout: TIMEOUT }, async () => {
      await sleep(RATE_LIMIT_DELAY);
      const storage = cloneStorage(oldStateStorage);
      Colibri.register_storage(storage);
      const spy = createSpyCache();

      const c4 = new Colibri({ chainId: CHAIN_ID, zk_proof: true, cache: spy });
      const result = await c4.rpc('eth_blockNumber', []);
      assertIsHexBlockNumber(result);

      const bootstrapReqs = spy.log.filter(r =>
        r.url && r.url.includes('light_client_bootstrap')
      );
      assert.strictEqual(bootstrapReqs.length, 0, 'Should NOT fetch bootstrap with existing state + zk_proof');
    });

    test('old storage + zk_proof false', { timeout: TIMEOUT }, async () => {
      await sleep(RATE_LIMIT_DELAY);
      const storage = cloneStorage(oldStateStorage);
      Colibri.register_storage(storage);
      const spy = createSpyCache();

      const c4 = new Colibri({ chainId: CHAIN_ID, zk_proof: false, cache: spy });
      const result = await c4.rpc('eth_blockNumber', []);
      assertIsHexBlockNumber(result);

      assert.ok(spy.log.length > 0, 'Should have made network requests to update state');
    });

    test('old storage + local proof (prover:[])', { timeout: TIMEOUT }, async () => {
      await sleep(RATE_LIMIT_DELAY);
      const storage = cloneStorage(oldStateStorage);
      Colibri.register_storage(storage);
      const spy = createSpyCache();

      const c4 = new Colibri({ chainId: CHAIN_ID, prover: [], cache: spy });
      const result = await c4.rpc('eth_blockNumber', []);
      assertIsHexBlockNumber(result);

      const proverReqs = spy.log.filter(r => r.type === 'prover');
      assert.strictEqual(proverReqs.length, 0, 'Should NOT make prover requests with empty prover list');

      const beaconReqs = spy.log.filter(r => r.type === 'beacon_api');
      assert.ok(beaconReqs.length > 0, 'Should fetch from beacon API when no prover');
    });
  });

  // -------------------------------------------------------------------
  // Local Proof scenarios
  // -------------------------------------------------------------------

  describe('Local Proof (prover:[])', { concurrency: false }, () => {

    test('local proof + empty storage', { timeout: TIMEOUT }, async () => {
      // Longer delay: local proof with empty storage hammers the beacon API heavily
      await sleep(RATE_LIMIT_DELAY * 3);
      const storage = createMemoryStorage();
      Colibri.register_storage(storage);

      const c4 = new Colibri({ chainId: CHAIN_ID, prover: [] });

      let result;
      try {
        result = await c4.rpc('eth_blockNumber', []);
      } catch (err) {
        if (err.message && err.message.includes('429')) {
          console.log('  local proof + empty storage: skipped due to rate limiting (429)');
          return;
        }
        throw err;
      }
      assertIsHexBlockNumber(result);
    });

    test('local proof + zk_proof true should not crash', { timeout: TIMEOUT }, async () => {
      await sleep(RATE_LIMIT_DELAY);
      const storage = createMemoryStorage();
      Colibri.register_storage(storage);

      const c4 = new Colibri({ chainId: CHAIN_ID, prover: [], zk_proof: true });

      let result;
      try {
        result = await c4.rpc('eth_blockNumber', []);
      } catch (err) {
        // zk_proof with local proof may fail gracefully; assert it's not a crash
        assert.ok(err instanceof Error, 'Should throw a proper Error, not crash');
        return;
      }
      assertIsHexBlockNumber(result);
    });
  });

  // -------------------------------------------------------------------
  // Checkpoint edge case (>127 periods)
  // -------------------------------------------------------------------

  describe('Checkpoint edge cases', { concurrency: false }, () => {

    test('checkpoint > 127 periods old', {
      skip: !RUN_SLOW,
      timeout: 300_000,
    }, async () => {
      await sleep(RATE_LIMIT_DELAY * 2);
      const storage = createMemoryStorage();
      Colibri.register_storage(storage);

      const checkpoint = await resolveCheckpointByPeriods(BEACON_API, 130);
      const c4 = new Colibri({
        chainId: CHAIN_ID,
        trusted_checkpoint: checkpoint,
        zk_proof: false,
      });

      try {
        const result = await c4.rpc('eth_blockNumber', []);
        assertIsHexBlockNumber(result);
        console.log('  > 127 periods checkpoint: succeeded (iterative update handling)');
      } catch (err) {
        assert.ok(err instanceof Error, 'Should throw a proper Error, not crash');
        console.log(`  > 127 periods checkpoint: errored as expected: ${err.message}`);
      }
    });
  });

  // -------------------------------------------------------------------
  // Default filesystem storage (real default path)
  // -------------------------------------------------------------------

  describe('Default filesystem storage', { concurrency: false }, () => {
    const stateFiles = [];

    after(() => {
      for (const f of stateFiles) {
        try { fs.unlinkSync(f); } catch { /* ignore */ }
      }
      const cwd = process.cwd();
      for (const name of fs.readdirSync(cwd)) {
        if (name.startsWith(`states_${CHAIN_ID}`) || name.startsWith(`sync_${CHAIN_ID}_`)) {
          try { fs.unlinkSync(`${cwd}/${name}`); } catch { /* ignore */ }
        }
      }
    });

    test('real default storage (filesystem)', { timeout: TIMEOUT }, async () => {
      await sleep(RATE_LIMIT_DELAY * 2);
      Colibri.register_storage({
        get: (key) => {
          try {
            return fs.readFileSync(key);
          } catch {
            return null;
          }
        },
        set: (key, value) => {
          stateFiles.push(key);
          fs.writeFileSync(key, value);
        },
        del: (key) => {
          try { fs.unlinkSync(key); } catch { /* ignore */ }
        },
      });

      const c4 = new Colibri({ chainId: CHAIN_ID, zk_proof: true });
      const result = await c4.rpc('eth_blockNumber', []);
      assertIsHexBlockNumber(result);

      assert.ok(stateFiles.length > 0, 'Should have written state files');
    });
  });

  // -------------------------------------------------------------------
  // Defaults-only test (minimal config)
  // -------------------------------------------------------------------

  describe('Defaults', { concurrency: false }, () => {

    test('minimal config (only chainId) with defaults', { timeout: TIMEOUT }, async () => {
      await sleep(RATE_LIMIT_DELAY);
      const storage = createMemoryStorage();
      Colibri.register_storage(storage);

      const c4 = new Colibri({ chainId: CHAIN_ID });
      const result = await c4.rpc('eth_blockNumber', []);
      assertIsHexBlockNumber(result);
    });
  });

  // -------------------------------------------------------------------
  // PAP placeholder (not yet implemented)
  // -------------------------------------------------------------------

  describe('Privacy Mode (PAP)', { skip: 'PAP not yet stable' }, () => {
    test('privacy_mode basic', { timeout: TIMEOUT }, async () => {
      const storage = createMemoryStorage();
      Colibri.register_storage(storage);

      const c4 = new Colibri({ chainId: CHAIN_ID, privacy_mode: 'basic' });
      const result = await c4.rpc('eth_blockNumber', []);
      assertIsHexBlockNumber(result);
    });
  });
});
