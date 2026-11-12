/*
 * Copyright (c) 2025,2026 corpus.core GmbH
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

// Synthetic proofs for tests.
//
// - `synth_account_proof`: an `EthAccountProof` of one contract with one storage slot in
//   small Merkle-Patricia tries built with the trie builder of the verifier. Every
//   rejection path of a storage-slot proof can be reached with an otherwise consistent proof.
// - `synth_eip2935_case`: an `eip2935` block proof on top of it: a history contract, a
//   newer header that commits to its state and an older header whose hash the slot holds.

#ifndef synth_proofs_h__
#define synth_proofs_h__

#include "beacon_types.h"
#include "bytes.h"
#include "crypto.h"
#include "eip2935.h"
#include "patricia.h"
#include "rlp.h"
#include "ssz.h"
#include "unity.h"
#include "verify.h"
#include <string.h>

typedef struct {
  const uint8_t* address;            // account in the trie and `address` field; NULL = the contract of the call
  const uint8_t* key;                // `key` field of the storage proof; NULL = the slot of the call
  int            storage_proofs;     // entries in `storageProof`; -1 = one
  int            account_fields;     // RLP fields of the account; 0 = four
  bool           slot_missing;       // the slot is not part of the storage trie
  bool           truncate_storage;   // drop the leaf node of the storage proof
  bool           truncate_account;   // drop the leaf node of the account proof
  bool           wrong_storage_hash; // the account commits to another storage trie
  bool           empty_storage_proof;
  int            storage_hash_len; // length of the storage hash inside the account; 0 = 32
  bool           leaf_is_list;     // the storage leaf holds an RLP list instead of a byte string
  bytes_t        el_proof;         // serialized `elProof` union (selector + value); empty = `blockHash` of 32 zero bytes
} synth_opts_t;

static const synth_opts_t SYNTH_VALID = {.storage_proofs = -1};

typedef struct {
  ssz_ob_t  account;    // `EthAccountProof`, bytes owned
  bytes32_t state_root; // root of the account trie
} synth_t;

// Fills a trie with unrelated entries so the proven key sits below branch nodes.
static void synth_add_filler(node_t** root, const char* domain) {
  for (uint32_t i = 0; i < 24; i++) {
    bytes32_t path  = {0};
    buffer_t  seed  = {0};
    buffer_t  value = {0};
    bprintf(&seed, "%s:%d", domain, i);
    keccak(seed.data, path);
    rlp_add_uint64(&value, (uint64_t) i + 1000);
    patricia_set_value(root, bytes(path, 32), value.data);
    buffer_free(&seed);
    buffer_free(&value);
  }
}

// Copy of a list of byte strings without its last element.
static ssz_ob_t synth_without_last(ssz_ob_t list) {
  uint32_t      len     = ssz_len(list);
  ssz_builder_t builder = {.def = list.def};
  TEST_ASSERT_TRUE(len >= 2);
  for (uint32_t i = 0; i + 1 < len; i++)
    ssz_add_dynamic_list_bytes(&builder, (int) (len - 1), ssz_at(list, i).bytes);
  return ssz_builder_to_bytes(&builder);
}

// Builds an `EthAccountProof` for the storage slot `slot` of the contract `address`
// holding `value`, with the deviations selected in `opts`. Both tries are filled with
// unrelated entries, so the proofs run through branch nodes.
static synth_t synth_account_proof(const uint8_t* contract, const uint8_t* slot, bytes_t value, synth_opts_t opts) {
  synth_t        out            = {0};
  bytes32_t      path           = {0};
  bytes32_t      other          = {0};
  buffer_t       buf            = {0};
  node_t*        storage        = NULL;
  node_t*        accounts       = NULL;
  const uint8_t* address        = opts.address ? opts.address : contract;
  int            storage_proofs = opts.storage_proofs < 0 ? 1 : opts.storage_proofs;
  int            fields         = opts.account_fields ? opts.account_fields : 4;

  // storage trie
  synth_add_filler(&storage, "storage");
  if (!opts.slot_missing) {
    keccak(bytes((uint8_t*) slot, 32), path);
    rlp_add_item(&buf, value);
    if (opts.leaf_is_list) rlp_to_list(&buf);
    patricia_set_value(&storage, bytes(path, 32), buf.data);
    buffer_reset(&buf);
  }
  keccak(bytes((uint8_t*) slot, 32), path);
  ssz_ob_t storage_proof = patricia_create_merkle_proof(storage, bytes(path, 32));
  if (opts.truncate_storage || opts.empty_storage_proof) {
    ssz_ob_t full = storage_proof;
    if (opts.empty_storage_proof)
      storage_proof = (ssz_ob_t) {.def = full.def, .bytes = bytes(safe_calloc(1, 1), 0)};
    else
      storage_proof = synth_without_last(full);
    safe_free(full.bytes.data);
  }

  // account: [nonce, balance, storageHash, codeHash]
  memset(other, 0x5a, 32);
  rlp_add_uint64(&buf, 1);
  rlp_add_uint64(&buf, 0);
  rlp_add_item(&buf, opts.wrong_storage_hash ? bytes(other, 32) : bytes(patricia_get_root(storage).data, opts.storage_hash_len ? (uint32_t) opts.storage_hash_len : 32));
  rlp_add_item(&buf, bytes(other, 32));
  for (int i = 4; i < fields; i++) rlp_add_uint64(&buf, 7);
  rlp_to_list(&buf);

  // account trie
  synth_add_filler(&accounts, "accounts");
  keccak(bytes((uint8_t*) address, 20), path);
  patricia_set_value(&accounts, bytes(path, 32), buf.data);
  memcpy(out.state_root, patricia_get_root(accounts).data, 32);
  ssz_ob_t account_proof = patricia_create_merkle_proof(accounts, bytes(path, 32));
  if (opts.truncate_account) {
    ssz_ob_t full = account_proof;
    account_proof = synth_without_last(full);
    safe_free(full.bytes.data);
  }

  // EthAccountProof
  ssz_builder_t    account      = ssz_builder_for_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF);
  const ssz_def_t* storage_def  = ssz_get_def(account.def, "storageProof");
  ssz_builder_t    storage_list = {.def = storage_def};
  uint8_t          no_block[33] = {0}; // selector 0 (`blockHash`) + 32 zero bytes
  bytes_t          el_proof     = opts.el_proof.data ? opts.el_proof : bytes(no_block, 33);
  for (int i = 0; i < storage_proofs; i++) {
    ssz_builder_t entry = {.def = storage_def->def.vector.type};
    ssz_add_bytes(&entry, "key", bytes((uint8_t*) (opts.key ? opts.key : slot), 32));
    ssz_add_bytes(&entry, "proof", storage_proof.bytes);
    ssz_add_dynamic_list_builders(&storage_list, storage_proofs, entry);
  }
  ssz_add_bytes(&account, "accountProof", account_proof.bytes);
  ssz_add_bytes(&account, "address", bytes((uint8_t*) address, 20));
  ssz_add_builders(&account, "storageProof", storage_list);
  ssz_add_bytes(&account, "elProof", el_proof);
  out.account = ssz_builder_to_bytes(&account);

  safe_free(storage_proof.bytes.data);
  safe_free(account_proof.bytes.data);
  patricia_node_free(storage);
  patricia_node_free(accounts);
  buffer_free(&buf);
  return out;
}

// ---------------------------------------------------------------------------
// eip2935 block proofs
// ---------------------------------------------------------------------------

// number of the newer block unless a test picks another one; 1,000,000 % 8191 = 698
#define SYNTH_EIP2935_NEWER 1000000

// RLP header (Cancun layout, 20 fields) of block `number` with the given state root.
// `seed` makes headers of the same height differ. The caller owns the bytes.
static bytes_t synth_header(uint64_t number, const uint8_t* state_root, uint8_t seed) {
  buffer_t  buf        = {0};
  bytes32_t word       = {0};
  uint8_t   bloom[256] = {0};
  uint8_t   nonce[8]   = {0};
  memset(word, seed, 32);
  rlp_add_item(&buf, bytes(word, 32));                  // parentHash
  rlp_add_item(&buf, bytes(word, 32));                  // sha3Uncles
  rlp_add_item(&buf, bytes(word, 20));                  // miner
  rlp_add_item(&buf, bytes((uint8_t*) state_root, 32)); // stateRoot
  rlp_add_item(&buf, bytes(word, 32));                  // transactionsRoot
  rlp_add_item(&buf, bytes(word, 32));                  // receiptsRoot
  rlp_add_item(&buf, bytes(bloom, 256));                // logsBloom
  rlp_add_uint64(&buf, 0);                              // difficulty
  rlp_add_uint64(&buf, number);                         // number
  rlp_add_uint64(&buf, 30000000);                       // gasLimit
  rlp_add_uint64(&buf, 0);                              // gasUsed
  rlp_add_uint64(&buf, 1700000000 + number * 2);        // timestamp
  rlp_add_item(&buf, NULL_BYTES);                       // extraData
  rlp_add_item(&buf, bytes(word, 32));                  // mixHash
  rlp_add_item(&buf, bytes(nonce, 8));                  // nonce
  rlp_add_uint64(&buf, 7);                              // baseFeePerGas
  rlp_add_item(&buf, bytes(word, 32));                  // withdrawalsRoot
  rlp_add_uint64(&buf, 0);                              // blobGasUsed
  rlp_add_uint64(&buf, 0);                              // excessBlobGas
  rlp_add_item(&buf, bytes(word, 32));                  // parentBeaconBlockRoot
  rlp_to_list(&buf);
  return buf.data;
}

typedef struct {
  uint64_t       older;          // number of the older block
  uint64_t       newer;          // number of the newer block; 0 = SYNTH_EIP2935_NEWER
  const uint8_t* stored;         // 32-byte value of the history slot; NULL = hash of the older header
  bytes_t        older_header;   // header sent as `elHeader`; empty = a synthetic header of `older`
  bool           slot_of_next;   // the proof is about the slot of block `older + 1`
  bool           other_state;    // the newer header commits to another state root
  bool           unknown_newer;  // the newer block is not a verified header of the context
  uint8_t        inner_selector; // selector of `account.elProof`; 0 = `blockHash`
  synth_opts_t   account;        // deviations of the account proof
} synth_eip2935_opts_t;

typedef struct {
  ssz_ob_t     proof;        // `eip2935` union member, bytes owned
  bytes_t      older_header; // owned
  bytes32_t    older_hash;
  bytes_t      newer_header; // owned copy of the newer header
  bytes32_t    newer_hash;
  verify_ctx_t ctx; // holds the newer header as a verified snapshot
} synth_eip2935_case_t;

// Builds an `eip2935` proof for `opts` and a verify context of `chain` that already
// verified the newer block (attached as a snapshot, the way `blockHash` proofs find it).
static synth_eip2935_case_t synth_eip2935_case(synth_eip2935_opts_t opts, chain_id_t chain) {
  synth_eip2935_case_t out       = {.ctx = {.chain_id = chain}};
  uint64_t             newer     = opts.newer ? opts.newer : SYNTH_EIP2935_NEWER;
  bytes32_t            slot      = {0};
  bytes32_t            other     = {0};
  bytes32_t            state     = {0};
  uint8_t              inner[33] = {0};

  // older header and the value of its history slot
  memset(state, 0x11, 32);
  out.older_header = opts.older_header.data ? bytes_dup(opts.older_header) : synth_header(opts.older, state, 0x22);
  keccak(out.older_header, out.older_hash);
  eth_eip2935_slot(opts.slot_of_next ? opts.older + 1 : opts.older, slot);

  // history contract in the state of the newer block
  synth_opts_t account_opts = opts.account;
  inner[0]                  = opts.inner_selector;
  account_opts.el_proof     = bytes(inner, 33); // the hash is patched in below
  synth_t synth             = synth_account_proof(ETH_EIP2935_HISTORY_ADDRESS, slot, bytes((uint8_t*) (opts.stored ? opts.stored : out.older_hash), 32), account_opts);

  // newer header, referenced by hash
  memset(other, 0x33, 32);
  bytes_t newer_header = synth_header(newer, opts.other_state ? other : synth.state_root, 0x44);
  keccak(newer_header, out.newer_hash);
  ssz_ob_t el_proof = ssz_get(&synth.account, "elProof");
  if (opts.inner_selector == 0) {
    TEST_ASSERT_EQUAL_STRING("blockHash", el_proof.def->name);
    TEST_ASSERT_EQUAL_UINT32(32, el_proof.bytes.len);
    memcpy(el_proof.bytes.data, out.newer_hash, 32);
  }
  out.newer_header = bytes_dup(newer_header);
  if (opts.unknown_newer)
    safe_free(newer_header.data);
  else
    c4_state_cache_set(&out.ctx.state, out.newer_hash, newer_header); // owned by the state from here on

  ssz_builder_t proof = ssz_builder_for_type(ETH_SSZ_EIP2935_PROOF);
  ssz_add_bytes(&proof, "elHeader", out.older_header);
  ssz_add_bytes(&proof, "account", synth.account.bytes);
  out.proof = ssz_builder_to_bytes(&proof);
  safe_free(synth.account.bytes.data);
  TEST_ASSERT_EQUAL_STRING("eip2935", out.proof.def->name);
  return out;
}

static void synth_eip2935_free(synth_eip2935_case_t* c) {
  c4_verify_free_data(&c->ctx);
  safe_free(c->proof.bytes.data);
  safe_free(c->older_header.data);
  safe_free(c->newer_header.data);
}

#endif
