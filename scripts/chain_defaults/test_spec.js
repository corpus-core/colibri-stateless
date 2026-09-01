#!/usr/bin/env node
/**
 * Source-of-truth checks for scripts/chain_defaults/chains.json.
 * Does not hit the network. Pair with generate.js --check in CI.
 */

const assert = require('assert');
const path = require('path');
const spec = require('./chains.json');

const DEAD_URL_SNIPPETS = [
  'sepolia.drpc.org',
  'sepolia-prover.incubed.net',
  'sepolia.colimind.com',
  'gnosis-prover.incubed.net',
  'gnosis.colimind.com',
];

const EXPECTED_PROVER_HEAD = {
  1: ['https://mainnet.colibri-proof.tech', 'https://mainnet1.colibri-proof.tech'],
  11155111: ['https://sepolia.colibri-proof.tech', 'https://sepolia1.colibri-proof.tech'],
  100: ['https://gnosis.colibri-proof.tech', 'https://gnosis1.colibri-proof.tech'],
};

function allUrls(chain) {
  return [].concat(chain.eth_rpc, chain.beacon_api, chain.checkpointz, chain.prover);
}

assert(Array.isArray(spec.chains) && spec.chains.length > 0, 'chains array required');
assert(Array.isArray(spec.unknown_chain_prover_fallback) && spec.unknown_chain_prover_fallback.length > 0);

const byId = new Map(spec.chains.map((c) => [c.id, c]));
const plataberget = byId.get(7091047534);
assert(plataberget, 'plataberget (7091047534) must be present');
for (const alias of ['plataberget', 'glamsterdam-devnet-8', '0x1a6a8cc6e']) {
  assert(plataberget.aliases.includes(alias), `plataberget missing alias ${alias}`);
}
assert.deepStrictEqual(plataberget.prover, ['https://plataberget.colibri-proof.tech']);
assert(plataberget.eth_rpc[0].includes('/execution'));
assert(plataberget.beacon_api[0].includes('/consensus'));

for (const [id, head] of Object.entries(EXPECTED_PROVER_HEAD)) {
  const chain = byId.get(Number(id));
  assert(chain, `missing chain ${id}`);
  assert.strictEqual(chain.prover[0], head[0], `${chain.name} cloudflare prover first`);
  assert.strictEqual(chain.prover[1], head[1], `${chain.name} *1 prover second`);
}

for (const chain of spec.chains) {
  for (const url of allUrls(chain)) {
    for (const dead of DEAD_URL_SNIPPETS) {
      assert(!url.includes(dead), `${chain.name} still lists dead URL ${dead}`);
    }
  }
}

console.log(`OK ${path.basename(__filename)}: ${spec.chains.length} chains`);
