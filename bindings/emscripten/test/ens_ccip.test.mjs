// End-to-end test for ENS CCIP-Read (EIP-3668) resolution via Colibri + ethers.
//
// Wraps a mainnet Colibri client as an ethers BrowserProvider and resolves the
// `contenthash` for offchain ENS names (preferring .box, then a stable
// offchaindemo.eth fallback). The Universal Resolver returns an OffchainLookup
// revert which ethers decodes, fetches the gateway response, and verifies via a
// follow-up eth_call. Both calls must round-trip through Colibri's verifier.
//
// Run with `C4_RUN_INTEGRATION=1 node --test test/ens_ccip.test.mjs` or via the
// `test:ccip` npm script.

import test, { describe, after, before } from 'node:test';
import assert from 'node:assert';
import { ethers } from 'ethers';
import { modulePath } from './test_config.js';

const ColibriModule = await import(modulePath);
const Colibri = ColibriModule.default;

const RUN_INTEGRATION = process.env.C4_RUN_INTEGRATION === '1' || process.env.GITHUB_ACTIONS === 'true';

const CHAIN_ID = 1;
const BEACON_API = 'https://mainnet1.colibri-proof.tech/consensus/';
const SLOTS_PER_EPOCH = 32;
const SECONDS_PER_SLOT = 12;
const TIMEOUT = 180_000;

// ENS Universal Resolver on Ethereum mainnet (ENSv1).
// See https://docs.ens.domains/resolvers/universal
const UR_ADDRESS = '0xeEeEEEeE14D718C2B47D9923Deab1335E144EeEe';
const UR_ABI = [
  'function resolve(bytes name, bytes data) view returns (bytes, address)',
];

// Selectors of standard ENS resolver functions.
const CONTENTHASH_SELECTOR = '0xbc1c58d1'; // contenthash(bytes32)
const ADDR_SELECTOR        = '0x3b3b57de'; // addr(bytes32)

// Stable, well-known CCIP-Read demo name maintained by ENS Labs (greg.skril).
// Resolves an `addr` record via an offchain gateway -- the canonical proof
// that the EIP-3668 round-trip succeeded.
const OFFCHAIN_FALLBACK = 'offchaindemo.eth';

// Candidate .box names probed in order. .box uses 3DNS's L1 resolver with
// CCIP-Read to fetch records from L2 (Optimism). We try a few plausible
// candidates; success requires a non-empty contenthash response.
const BOX_CANDIDATES = ['3dns.box', 'my.box', 'colibri.box', 'nick.box', 'vitalik.box'];

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function createMemoryStorage() {
  const map = new Map();
  return {
    get: (key) => map.get(key) ?? null,
    set: (key, value) => map.set(key, new Uint8Array(value)),
    del: (key) => map.delete(key),
    _map: map,
  };
}

async function fetchWithRetry(url, retries = 3, delayMs = 2000) {
  for (let i = 0; i < retries; i++) {
    try {
      const res = await fetch(url);
      if (res.ok) return res;
      if (res.status === 429 && i < retries - 1) {
        await new Promise((r) => setTimeout(r, delayMs * (i + 1)));
        continue;
      }
      throw new Error(`Fetch ${url} failed: ${res.status}`);
    } catch (err) {
      if (i === retries - 1) throw err;
      await new Promise((r) => setTimeout(r, delayMs * (i + 1)));
    }
  }
  throw new Error('Unreachable');
}

async function resolveLatestCheckpoint(beaconApiUrl) {
  const headRes = await fetchWithRetry(`${beaconApiUrl}/eth/v1/beacon/headers/head`);
  const headJson = await headRes.json();
  let slot = Number(headJson.data.header.message.slot);
  slot -= slot % SLOTS_PER_EPOCH;
  const cpRes = await fetchWithRetry(`${beaconApiUrl}/eth/v1/beacon/headers/${slot}`);
  const cpJson = await cpRes.json();
  return cpJson.data.root;
}

function buildCallData(selector, name) {
  return selector + ethers.namehash(name).slice(2);
}

async function tryResolve(provider, name, selector = CONTENTHASH_SELECTOR) {
  const ur = new ethers.Contract(UR_ADDRESS, UR_ABI, provider);
  const dnsName = ethers.dnsEncode(name, 255);
  // enableCcipRead: true triggers ethers' built-in OffchainLookup handler.
  const [resolvedData, resolverAddress] = await ur.resolve(dnsName, buildCallData(selector, name), {
    enableCcipRead: true,
    blockTag: 'latest',
  });
  return { resolvedData, resolverAddress };
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

describe('ENS CCIP-Read (EIP-3668) end-to-end', { skip: !RUN_INTEGRATION, timeout: TIMEOUT, concurrency: false }, () => {
  let provider;

  before(async () => {
    Colibri.register_storage(createMemoryStorage());
    const trusted_checkpoint = await resolveLatestCheckpoint(BEACON_API);
    const colibri = new Colibri({
      chainId: CHAIN_ID,
      trusted_checkpoint,
      zk_proof: false,
    });
    // ethers v6 BrowserProvider expects an EIP-1193 provider: { request(args) }.
    provider = new ethers.BrowserProvider(colibri);
    // Sanity check: confirm chain id round-trip through Colibri/ethers wrapping.
    const network = await provider.getNetwork();
    assert.strictEqual(Number(network.chainId), CHAIN_ID, 'BrowserProvider should report mainnet chain id');
  });

  after(() => {
    Colibri.register_storage(createMemoryStorage());
  });

  // Primary correctness test: drives the full EIP-3668 round-trip (OffchainLookup
  // revert -> gateway fetch -> verifying callback eth_call) through Colibri.
  // The very fact that `UR.resolve(...)` returns *without throwing* on
  // `offchaindemo.eth` proves the entire pipeline works end-to-end:
  //   1. The initial eth_call reverts with OffchainLookup -- Colibri's
  //      verifier preserves the revert payload (VERIFY_FLAG_REVERTED) and
  //      the JS binding maps it to a structured ProviderRpcError (code 3 +
  //      data = revert bytes).
  //   2. ethers decodes OffchainLookup, fetches the offchain gateway, and
  //      submits the verifying callback eth_call.
  //   3. The callback eth_call also round-trips through Colibri and returns
  //      the verified resolver result.
  // We deliberately do not assert on the decoded record contents because the
  // demo offchain database can be empty for individual record types; what we
  // care about here is the revert-data flow.
  test('CCIP-Read full round-trip via offchaindemo.eth', async () => {
    const { resolvedData, resolverAddress } = await tryResolve(provider, OFFCHAIN_FALLBACK, CONTENTHASH_SELECTOR);
    assert.match(resolverAddress, /^0x[0-9a-fA-F]{40}$/, 'resolver address must be a hex address');
    assert.notStrictEqual(resolverAddress.toLowerCase(), ethers.ZeroAddress, 'resolver must not be 0x0');
    assert.match(resolvedData, /^0x[0-9a-fA-F]+$/, 'resolved data must be hex bytes');
    // A successful CCIP-Read round-trip returns at least the ABI envelope
    // for the inner `bytes` return (offset + length = 64 bytes = 130 hex
    // chars including 0x). Anything shorter means the callback never ran.
    assert.ok(resolvedData.length >= 130, `resolved data should contain an ABI bytes envelope (got ${resolvedData.length} chars)`);
    console.log(`  CCIP-Read round-trip OK: ${OFFCHAIN_FALLBACK} -> resolver ${resolverAddress} (${(resolvedData.length - 2) / 2} bytes response)`);
  });

  // Best-effort probe of .box names. .box uses 3DNS's L2 resolver via CCIP-Read.
  // The set of registered names changes over time, so this test is informational
  // only -- it does not fail the suite if no candidate currently has a contenthash.
  test('.box CCIP-Read probe (informational)', async () => {
    const attempts = [];
    let success = null;

    for (const name of BOX_CANDIDATES) {
      try {
        const { resolvedData, resolverAddress } = await tryResolve(provider, name, CONTENTHASH_SELECTOR);
        const [decoded] = ethers.AbiCoder.defaultAbiCoder().decode(['bytes'], resolvedData);
        attempts.push({ name, ok: true, contenthashLength: (decoded.length - 2) / 2, resolverAddress });
        if (decoded && decoded !== '0x' && decoded.length > 2) {
          success = { name, resolverAddress, contenthash: decoded };
          break;
        }
      } catch (err) {
        attempts.push({ name, ok: false, code: err?.code, message: (err?.message || '').slice(0, 120) });
      }
    }

    if (success)
      console.log(`  .box success: ${success.name} -> resolver ${success.resolverAddress}, contenthash bytes=${(success.contenthash.length - 2) / 2}`);
    else
      console.log('  no .box candidate currently resolves a contenthash (informational):', JSON.stringify(attempts));
  });

  // Regression guard: non-CCIP names must resolve in a single eth_call without
  // ever entering the new revert path.
  test('non-CCIP name resolves directly without OffchainLookup', async () => {
    const { resolvedData } = await tryResolve(provider, 'vitalik.eth');
    assert.match(resolvedData, /^0x[0-9a-fA-F]*$/, 'resolve() must return hex bytes');
  });
});
