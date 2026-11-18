/*
 * Copyright (c) 2025,2026 corpus.core GmbH
 * SPDX-License-Identifier: MIT
 */

#include "unity.h"

#ifdef NO_CHAIN_ARB

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
  return UNITY_END();
}

#else

#include "arb_batch_poster.h"
#include "arb_prover.h"
#include "arb_verify.h"
#include "beacon_types.h"
#include "bytes.h"
#include "crypto.h"
#include "historic_proof.h"
#include "plugin.h"
#include "ssz.h"
#include "state.h"
#include <stdlib.h>
#include <string.h>

static const uint8_t KNOWN_POSTER[20] = {
    0xC1, 0xb6, 0x34, 0x85, 0x3C, 0xb3, 0x33, 0xD3, 0xaD, 0x86,
    0x63, 0x71, 0x5b, 0x08, 0xf4, 0x1A, 0x3A, 0xec, 0x47, 0xcc};

static void clear_poster(chain_id_t chain_id) {
  char             name[100] = {0};
  storage_plugin_t storage   = {0};
  c4_get_storage_config(&storage);
  sbprintf(name, "batch_poster_%l", (uint64_t) chain_id);
  if (storage.del) storage.del(name);
}

void setUp(void) {
  clear_poster(C4_CHAIN_ARB_ONE);
  clear_poster(C4_CHAIN_MAINNET);
}

void tearDown(void) {
  clear_poster(C4_CHAIN_ARB_ONE);
  clear_poster(C4_CHAIN_MAINNET);
  c4_register_sync_data_prover(C4_CHAIN_TYPE_ARBITRUM, arb_batch_poster_sync);
}

static const uint8_t INBOX[20] = {
    0x1c, 0x47, 0x96, 0x75, 0xad, 0x55, 0x9d, 0xc1, 0x51, 0xf6,
    0xec, 0x7e, 0xd3, 0xfb, 0xf8, 0xce, 0xe7, 0x95, 0x82, 0xb6};

static void zero_container(ssz_builder_t* builder) {
  const ssz_def_t* elements = ssz_container_elements(builder->def);
  uint32_t         count    = ssz_container_len(builder->def);
  for (uint32_t i = 0; i < count; i++)
    ssz_add_bytes(builder, elements[i].name, NULL_BYTES);
}

static ssz_builder_t cl_proof_builder(void) {
  const ssz_def_t* cl     = eth_ssz_verification_type(ETH_SSZ_CL_HEADER_PROOF);
  ssz_builder_t    header = {.def = ssz_get_def(cl, "clHeader")};
  zero_container(&header);

  const ssz_def_t* proof_union = ssz_get_def(cl, "clHeaderProof");
  ssz_builder_t    signature   = {.def = &proof_union->def.container.elements[0]};
  zero_container(&signature);

  ssz_builder_t cl_proof = {.def = cl};
  ssz_add_bytes(&cl_proof, "elHeader", NULL_BYTES);
  ssz_add_builders(&cl_proof, "clHeader", header);
  ssz_add_bytes(&cl_proof, "blockhashBranch", NULL_BYTES);
  ssz_add_uint64(&cl_proof, 812);
  ssz_add_builders(&cl_proof, "clHeaderProof", signature);
  return cl_proof;
}

static void add_storage_list(ssz_builder_t* account, const uint8_t key[32], int count) {
  const ssz_def_t* list_def = ssz_get_def(account->def, "storageProof");
  if (count <= 0) {
    ssz_add_bytes(account, "storageProof", NULL_BYTES);
    return;
  }
  ssz_builder_t list = {.def = list_def};
  for (int i = 0; i < count; i++) {
    ssz_builder_t entry = {.def = list_def->def.vector.type};
    ssz_add_bytes(&entry, "key", bytes((uint8_t*) key, 32));
    ssz_add_bytes(&entry, "proof", NULL_BYTES);
    ssz_add_dynamic_list_builders(&list, count, entry);
  }
  ssz_add_builders(account, "storageProof", list);
}

static ssz_builder_t account_builder(const uint8_t address[20], const uint8_t key[32], int storage_count, bool cl_proof) {
  ssz_builder_t account = ssz_builder_for_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF);
  ssz_add_bytes(&account, "accountProof", NULL_BYTES);
  ssz_add_bytes(&account, "address", bytes((uint8_t*) address, 20));
  add_storage_list(&account, key, storage_count);
  if (cl_proof)
    ssz_add_builders(&account, "elProof", cl_proof_builder());
  else {
    uint8_t raw[33] = {0};
    ssz_add_bytes(&account, "elProof", bytes(raw, 33));
  }
  return account;
}

static void add_request_sync(ssz_builder_t* req, int kind) {
  uint8_t none = 0;
  if (kind == 0) {
    ssz_add_bytes(req, "sync_data", bytes(&none, 1));
    return;
  }
  if (kind == 1) {
    ssz_builder_t lc = ssz_builder_for_type(ETH_SSZ_VERIFY_LC_SYNCDATA);
    ssz_add_bytes(&lc, "bootstrap", bytes(&none, 1));
    ssz_add_bytes(&lc, "update", NULL_BYTES);
    ssz_add_builders(req, "sync_data", lc);
    return;
  }
  uint8_t       slot[32] = {0};
  ssz_builder_t inner    = ssz_builder_for_type(ETH_SSZ_VERIFY_ARB_SIGNER_SYNC);
  arb_batch_poster_storage_key(KNOWN_POSTER, slot);
  ssz_builder_t inner_account = account_builder(INBOX, slot, 1, false);
  ssz_add_bytes(&inner, "signer", bytes((uint8_t*) KNOWN_POSTER, 20));
  ssz_add_builders(&inner, "account", inner_account);
  ssz_add_bytes(&inner, "sync_data", bytes(&none, 1));
  ssz_add_builders(req, "sync_data", inner);
}

// `kind`: 0 none, 1 light-client sync, 2 nested arbSignerSyncData.
static bytes_t l1_answer(const uint8_t address[20], const uint8_t key[32], int storage_count, bool cl_proof, int kind) {
  ssz_builder_t account    = account_builder(address, key, storage_count, cl_proof);
  ssz_builder_t req        = ssz_builder_for_type(ETH_SSZ_VERIFY_REQUEST);
  uint8_t       version[4] = {1, 0, 0, 0};
  uint8_t       none       = 0;
  ssz_add_bytes(&req, "version", bytes(version, 4));
  ssz_add_bytes(&req, "data", bytes(&none, 1));
  ssz_add_builders(&req, "proof", account);
  add_request_sync(&req, kind);
  ssz_ob_t   ob    = ssz_builder_to_bytes(&req);
  c4_state_t state = {0};
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(ob, true, &state), state.error ? state.error : "L1 answer must be valid SSZ");
  c4_state_free(&state);
  return ob.bytes;
}

static prover_ctx_t poster_ctx(chain_id_t chain) {
  prover_ctx_t ctx = {0};
  ctx.chain_id     = chain;
  ctx.flags        = C4_PROVER_FLAG_ARB_BATCH_POSTER;
  memcpy(ctx.batch_poster, KNOWN_POSTER, 20);
  return ctx;
}

static data_request_t* queued_request(prover_ctx_t* ctx) {
  ssz_builder_t ignored = {0};
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, arb_batch_poster_prepare(ctx));
  TEST_ASSERT_NOT_NULL(ctx->state.requests);
  TEST_ASSERT_NULL(ctx->state.requests->next);
  TEST_ASSERT_EQUAL_INT(C4_PENDING, arb_batch_poster_sync(ctx, &ignored));
  return ctx->state.requests;
}

static ssz_ob_t blockhash_signer_sync(void) {
  uint8_t       selector = 0;
  uint8_t       none     = 0;
  uint8_t       hash[32] = {0};
  buffer_t      el_proof = {0};
  ssz_builder_t account  = ssz_builder_for_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF);
  buffer_append(&el_proof, bytes(&selector, 1));
  buffer_append(&el_proof, bytes(hash, 32));
  ssz_add_bytes(&account, "accountProof", NULL_BYTES);
  ssz_add_bytes(&account, "address", bytes((uint8_t*) KNOWN_POSTER, 20));
  ssz_add_bytes(&account, "storageProof", NULL_BYTES);
  ssz_add_bytes(&account, "elProof", el_proof.data);
  ssz_ob_t account_ob = ssz_builder_to_bytes(&account);
  buffer_free(&el_proof);

  ssz_builder_t sync = ssz_builder_for_type(ETH_SSZ_VERIFY_ARB_SIGNER_SYNC);
  ssz_add_bytes(&sync, "signer", bytes((uint8_t*) KNOWN_POSTER, 20));
  ssz_add_bytes(&sync, "account", account_ob.bytes);
  ssz_add_bytes(&sync, "sync_data", bytes(&none, 1));
  safe_free(account_ob.bytes.data);
  return ssz_builder_to_bytes(&sync);
}

static int hook_calls = 0;

static c4_status_t counting_hook(prover_ctx_t* ctx, ssz_builder_t* builder) {
  (void) ctx;
  (void) builder;
  hook_calls++;
  return C4_SUCCESS;
}

static c4_status_t pending_hook(prover_ctx_t* ctx, ssz_builder_t* builder) {
  (void) ctx;
  (void) builder;
  return C4_PENDING;
}

static c4_status_t error_hook(prover_ctx_t* ctx, ssz_builder_t* builder) {
  (void) builder;
  THROW_ERROR("hook failed");
}

static c4_status_t def_hook(prover_ctx_t* ctx, ssz_builder_t* builder) {
  (void) ctx;
  builder->def = eth_ssz_verification_type(ETH_SSZ_VERIFY_ARB_SIGNER_SYNC);
  return C4_SUCCESS;
}

void test_batch_poster_storage_key(void) {
  uint8_t key[32] = {0};
  uint8_t expected[32];
  arb_batch_poster_storage_key(KNOWN_POSTER, key);
  TEST_ASSERT_EQUAL_INT(32, hex_to_bytes("a10aa54071443520884ed767b0684edf43acec528b7da83ab38ce60126562660", 64, bytes(expected, 32)));
  TEST_ASSERT_EQUAL_MEMORY(expected, key, 32);
}

void test_batch_poster_cache_roundtrip(void) {
  uint8_t  loaded[20] = {0};
  uint64_t timestamp  = 0;
  uint64_t number     = 0;
  TEST_ASSERT_FALSE(c4_get_cached_batch_poster(C4_CHAIN_ARB_ONE, loaded, &timestamp, &number));
  c4_set_cached_batch_poster(C4_CHAIN_ARB_ONE, KNOWN_POSTER, 42, 7);
  TEST_ASSERT_TRUE(c4_get_cached_batch_poster(C4_CHAIN_ARB_ONE, loaded, &timestamp, &number));
  TEST_ASSERT_EQUAL_MEMORY(KNOWN_POSTER, loaded, 20);
  TEST_ASSERT_EQUAL_UINT64(42, timestamp);
  TEST_ASSERT_EQUAL_UINT64(7, number);
}

void test_batch_poster_l1_number_is_monotonic(void) {
  TEST_ASSERT_TRUE(arb_batch_poster_l1_is_current(C4_CHAIN_ARB_ONE, 9));
  c4_set_cached_batch_poster(C4_CHAIN_ARB_ONE, KNOWN_POSTER, 1, 10);
  TEST_ASSERT_FALSE(arb_batch_poster_l1_is_current(C4_CHAIN_ARB_ONE, 9));
  TEST_ASSERT_TRUE(arb_batch_poster_l1_is_current(C4_CHAIN_ARB_ONE, 10));
  TEST_ASSERT_TRUE(arb_batch_poster_l1_is_current(C4_CHAIN_ARB_ONE, 11));
}

void test_append_sends_batch_poster_for_one_day(void) {
  uint64_t now = current_unix_ms();
  if (now <= C4_BATCH_POSTER_MAX_AGE_MS + 1000) TEST_IGNORE_MESSAGE("clock is before the 24h window");
  c4_set_cached_batch_poster(C4_CHAIN_ARB_ONE, KNOWN_POSTER, now - C4_BATCH_POSTER_MAX_AGE_MS + 1000, 3);

  buffer_t fresh = {0};
  bprintf(&fresh, "{");
  c4_append_prover_request_props(&fresh, NULL_BYTES, C4_CHAIN_ARB_ONE, 0, NULL_BYTES, NULL_BYTES);
  bprintf(&fresh, "}");
  TEST_ASSERT_NOT_NULL(strstr((char*) fresh.data.data, "\"batch_poster\":\"0xc1b634853cb333d3ad8663715b08f41a3aec47cc\""));
  buffer_free(&fresh);

  c4_set_cached_batch_poster(C4_CHAIN_ARB_ONE, KNOWN_POSTER, now - C4_BATCH_POSTER_MAX_AGE_MS - 1, 3);
  buffer_t stale = {0};
  bprintf(&stale, "{");
  c4_append_prover_request_props(&stale, NULL_BYTES, C4_CHAIN_ARB_ONE, 0, NULL_BYTES, NULL_BYTES);
  bprintf(&stale, "}");
  TEST_ASSERT_NULL(strstr((char*) stale.data.data, "batch_poster"));
  buffer_free(&stale);

  c4_set_cached_batch_poster(C4_CHAIN_MAINNET, KNOWN_POSTER, now, 3);
  buffer_t other = {0};
  bprintf(&other, "{");
  c4_append_prover_request_props(&other, NULL_BYTES, C4_CHAIN_MAINNET, 0, NULL_BYTES, NULL_BYTES);
  bprintf(&other, "}");
  TEST_ASSERT_NULL(strstr((char*) other.data.data, "batch_poster"));
  buffer_free(&other);
}

void test_signer_sync_is_union_index_3(void) {
  const ssz_def_t* sync = eth_ssz_verification_type(ETH_SSZ_VERIFY_ARB_SIGNER_SYNC);
  TEST_ASSERT_NOT_NULL(sync);
  TEST_ASSERT_EQUAL_STRING(ETH_SSZ_NAME_ARB_SIGNER_SYNC_DATA, sync->name);
  TEST_ASSERT_EQUAL_PTR(C4_ETH_REQUEST_SYNCDATA_UNION + 3, sync);
  TEST_ASSERT_EQUAL_STRING(ETH_SSZ_NAME_ARB_SIGNER_SYNC_DATA, C4_ETH_REQUEST_SYNCDATA_UNION[3].name);
}

void test_blockhash_signer_sync_restores_the_outer_object(void) {
  ssz_ob_t     sync_ob = blockhash_signer_sync();
  verify_ctx_t ctx     = {0};
  ctx.chain_id         = C4_CHAIN_ARB_ONE;
  ctx.sync_data        = sync_ob;
  TEST_ASSERT_TRUE(c4_arb_verify(&ctx));
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "batch poster proof must use a clProof"));
  TEST_ASSERT_NOT_NULL(ctx.sync_data.def);
  TEST_ASSERT_EQUAL_STRING(ETH_SSZ_NAME_ARB_SIGNER_SYNC_DATA, ctx.sync_data.def->name);
  TEST_ASSERT_EQUAL_UINT64(C4_CHAIN_ARB_ONE, ctx.chain_id);

  c4_state_free(&ctx.state);
  safe_free(sync_ob.bytes.data);
}

void test_sequencer_inbox_is_only_arbitrum_one(void) {
  uint8_t out[20];
  memset(out, 1, sizeof(out));
  TEST_ASSERT_FALSE(arb_sequencer_inbox(C4_CHAIN_ARB_ONE, NULL));
  TEST_ASSERT_FALSE(arb_sequencer_inbox(C4_CHAIN_MAINNET, out));
  TEST_ASSERT_EQUAL_HEX8(1, out[0]);
  TEST_ASSERT_TRUE(arb_sequencer_inbox(C4_CHAIN_ARB_ONE, out));
  TEST_ASSERT_EQUAL_MEMORY(INBOX, out, 20);
}

void test_cache_rejects_null_and_a_short_record(void) {
  uint8_t  loaded[20];
  uint64_t timestamp = 1;
  uint64_t number    = 1;
  memset(loaded, 0, sizeof(loaded));
  TEST_ASSERT_FALSE(c4_get_cached_batch_poster(C4_CHAIN_ARB_ONE, NULL, &timestamp, &number));
  TEST_ASSERT_FALSE(c4_get_cached_batch_poster(C4_CHAIN_ARB_ONE, loaded, NULL, &number));
  TEST_ASSERT_FALSE(c4_get_cached_batch_poster(C4_CHAIN_ARB_ONE, loaded, &timestamp, NULL));
  c4_set_cached_batch_poster(C4_CHAIN_ARB_ONE, NULL, 5, 6);
  TEST_ASSERT_FALSE(c4_get_cached_batch_poster(C4_CHAIN_ARB_ONE, loaded, &timestamp, &number));

  storage_plugin_t storage = {0};
  c4_get_storage_config(&storage);
  TEST_ASSERT_NOT_NULL(storage.set);
  uint8_t short_rec[10];
  memset(short_rec, 0xab, sizeof(short_rec));
  storage.set("batch_poster_42161", bytes(short_rec, sizeof(short_rec)));
  TEST_ASSERT_FALSE(c4_get_cached_batch_poster(C4_CHAIN_ARB_ONE, loaded, &timestamp, &number));
  TEST_ASSERT_TRUE(arb_batch_poster_l1_is_current(C4_CHAIN_ARB_ONE, 1));
}

void test_append_sends_a_future_or_exact_timestamp(void) {
  uint64_t now = current_unix_ms();
  c4_set_cached_batch_poster(C4_CHAIN_ARB_ONE, KNOWN_POSTER, now + 1000, 3);
  buffer_t future = {0};
  bprintf(&future, "{");
  c4_append_prover_request_props(&future, NULL_BYTES, C4_CHAIN_ARB_ONE, 0, NULL_BYTES, NULL_BYTES);
  bprintf(&future, "}");
  TEST_ASSERT_NOT_NULL(strstr((char*) future.data.data, "batch_poster"));
  buffer_free(&future);

  if (now <= C4_BATCH_POSTER_MAX_AGE_MS) TEST_IGNORE_MESSAGE("clock is before the 24h window");
  c4_set_cached_batch_poster(C4_CHAIN_ARB_ONE, KNOWN_POSTER, now - C4_BATCH_POSTER_MAX_AGE_MS, 3);
  buffer_t exact = {0};
  bprintf(&exact, "{");
  c4_append_prover_request_props(&exact, NULL_BYTES, C4_CHAIN_ARB_ONE, 0, NULL_BYTES, NULL_BYTES);
  bprintf(&exact, "}");
  TEST_ASSERT_NOT_NULL(strstr((char*) exact.data.data, "batch_poster"));
  buffer_free(&exact);
}

void test_storage_key_ignores_null(void) {
  uint8_t key[32];
  memset(key, 9, sizeof(key));
  arb_batch_poster_storage_key(NULL, key);
  TEST_ASSERT_EQUAL_HEX8(9, key[0]);
  arb_batch_poster_storage_key(KNOWN_POSTER, NULL);
}

void test_feed_recover_signer_accepts_both_v_ranges(void) {
  uint8_t hash[32];
  uint8_t tail[8];
  uint8_t sk[32] = {0};
  memset(hash, 0x11, sizeof(hash));
  memset(tail, 0x22, sizeof(tail));
  sk[31] = 1;
  bytes32_t digest;
  arb_feed_digest(42161, 9, hash, bytes(tail, sizeof(tail)), digest);
  uint8_t sig[65];
  TEST_ASSERT_TRUE(secp256k1_sign(sk, digest, sig));
  TEST_ASSERT_TRUE(sig[64] >= 27);

  uint8_t address[20] = {0};
  TEST_ASSERT_TRUE(arb_feed_recover_signer(42161, 9, hash, bytes(tail, sizeof(tail)), bytes(sig, 65), address));
  uint8_t pub[64] = {0};
  uint8_t kh[32]  = {0};
  TEST_ASSERT_TRUE(secp256k1_recover(digest, bytes(sig, 65), pub));
  keccak(bytes(pub, 64), kh);
  TEST_ASSERT_EQUAL_MEMORY(kh + 12, address, 20);

  sig[64]           = (uint8_t) (sig[64] - 27);
  uint8_t again[20] = {0};
  TEST_ASSERT_TRUE(arb_feed_recover_signer(42161, 9, hash, bytes(tail, sizeof(tail)), bytes(sig, 65), again));
  TEST_ASSERT_EQUAL_MEMORY(address, again, 20);
  TEST_ASSERT_FALSE(arb_feed_recover_signer(42161, 9, hash, bytes(tail, sizeof(tail)), bytes(sig, 64), address));
  TEST_ASSERT_FALSE(arb_feed_recover_signer(42161, 9, hash, bytes(tail, sizeof(tail)), bytes(sig, 65), NULL));
}

void test_flag_off_does_not_queue(void) {
  prover_ctx_t  ctx   = {0};
  ssz_builder_t built = {0};
  ctx.chain_id        = C4_CHAIN_ARB_ONE;
  memcpy(ctx.batch_poster, KNOWN_POSTER, 20);
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, arb_batch_poster_prepare(&ctx));
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, arb_batch_poster_sync(&ctx, &built));
  TEST_ASSERT_NULL(ctx.state.requests);
  TEST_ASSERT_NULL(ctx.state.error);
  TEST_ASSERT_NULL(built.def);
}

void test_missing_poster_and_unknown_chain_fail_before_a_request(void) {
  prover_ctx_t  ctx   = {0};
  ssz_builder_t built = {0};
  ctx.chain_id        = C4_CHAIN_ARB_ONE;
  ctx.flags           = C4_PROVER_FLAG_ARB_BATCH_POSTER;
  TEST_ASSERT_EQUAL_INT(C4_ERROR, arb_batch_poster_prepare(&ctx));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "batch poster address is missing"));
  TEST_ASSERT_NULL(ctx.state.requests);
  c4_state_free(&ctx.state);
  memset(&ctx.state, 0, sizeof(ctx.state));
  TEST_ASSERT_EQUAL_INT(C4_ERROR, arb_batch_poster_sync(&ctx, &built));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "batch poster address is missing"));
  c4_state_free(&ctx.state);

  ctx = poster_ctx(C4_CHAIN_MAINNET);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, arb_batch_poster_prepare(&ctx));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "no sequencer inbox for this chain"));
  TEST_ASSERT_NULL(ctx.state.requests);
  c4_state_free(&ctx.state);
}

void test_l1_request_targets_mainnet_and_omits_the_cached_poster(void) {
  c4_set_cached_batch_poster(C4_CHAIN_ARB_ONE, KNOWN_POSTER, current_unix_ms(), 9);
  uint8_t      state_bytes[2] = {0xab, 0xcd};
  uint8_t      signer[20]     = {0};
  prover_ctx_t ctx            = poster_ctx(C4_CHAIN_ARB_ONE);
  signer[0]                   = 0x11;
  ctx.client_state            = bytes(state_bytes, 2);
  ctx.witness_key             = bytes(signer, 20);
  ctx.flags |= C4_PROVER_FLAG_ZK_PROOF;
  memset(ctx.last_block_hash, 0xee, 32);

  data_request_t* req = queued_request(&ctx);
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, arb_batch_poster_prepare(&ctx));
  TEST_ASSERT_NULL(req->next);
  TEST_ASSERT_EQUAL_INT(C4_DATA_TYPE_PROVER, req->type);
  TEST_ASSERT_EQUAL_INT(C4_DATA_METHOD_POST, req->method);
  TEST_ASSERT_EQUAL_INT(C4_DATA_ENCODING_SSZ, req->encoding);
  TEST_ASSERT_EQUAL_UINT32(12, req->ttl);
  TEST_ASSERT_EQUAL_UINT64(C4_CHAIN_MAINNET, req->chain_id);
  TEST_ASSERT_NULL(req->url);
  TEST_ASSERT_NOT_NULL(req->payload.data);
  TEST_ASSERT_NOT_NULL(strstr((char*) req->payload.data, "\"method\":\"eth_getStorageAt\""));
  TEST_ASSERT_NOT_NULL(strstr((char*) req->payload.data, "0x1c479675ad559dc151f6ec7ed3fbf8cee79582b6"));
  TEST_ASSERT_NOT_NULL(strstr((char*) req->payload.data, "0xa10aa54071443520884ed767b0684edf43acec528b7da83ab38ce60126562660"));
  TEST_ASSERT_NOT_NULL(strstr((char*) req->payload.data, "\"latest\""));
  TEST_ASSERT_NOT_NULL(strstr((char*) req->payload.data, "\"zk_proof\":true"));
  TEST_ASSERT_NOT_NULL(strstr((char*) req->payload.data, "\"c4\":\"0xabcd\""));
  TEST_ASSERT_NOT_NULL(strstr((char*) req->payload.data, "\"signers\":\"0x11"));
  TEST_ASSERT_NULL(strstr((char*) req->payload.data, "batch_poster"));
  TEST_ASSERT_NULL(strstr((char*) req->payload.data, "last_block_hash"));
  c4_state_free(&ctx.state);

  ctx             = poster_ctx(C4_CHAIN_ARB_ONE);
  ctx.witness_key = bytes(signer, 19);
  req             = queued_request(&ctx);
  TEST_ASSERT_NULL(strstr((char*) req->payload.data, "signers"));
  c4_state_free(&ctx.state);
}

void test_failed_l1_answer_stops_prepare(void) {
  prover_ctx_t    ctx = poster_ctx(C4_CHAIN_ARB_ONE);
  data_request_t* req = queued_request(&ctx);
  req->error          = strdup("down");
  TEST_ASSERT_EQUAL_INT(C4_ERROR, arb_batch_poster_prepare(&ctx));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "L1 prover request for the batch poster failed"));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "down"));
  c4_state_free(&ctx.state);

  ctx           = poster_ctx(C4_CHAIN_ARB_ONE);
  req           = queued_request(&ctx);
  req->response = bytes_dup(bytes((uint8_t*) "{\"error\":true}", 14));
  TEST_ASSERT_EQUAL_INT(C4_ERROR, arb_batch_poster_prepare(&ctx));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "L1 prover rejected the batch poster request"));
  c4_state_free(&ctx.state);

  ctx                 = poster_ctx(C4_CHAIN_ARB_ONE);
  req                 = queued_request(&ctx);
  req->response       = bytes_dup(bytes((uint8_t*) "not-ssz", 7));
  ssz_builder_t built = {0};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, arb_batch_poster_sync(&ctx, &built));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "invalid response of the L1 prover"));
  c4_state_free(&ctx.state);
}

static void expect_answer_error(bytes_t answer, const char* fragment) {
  prover_ctx_t    ctx = poster_ctx(C4_CHAIN_ARB_ONE);
  data_request_t* req = queued_request(&ctx);
  req->response       = answer;
  ssz_builder_t built = {0};
  TEST_ASSERT_EQUAL_INT(C4_ERROR, arb_batch_poster_sync(&ctx, &built));
  TEST_ASSERT_NOT_NULL(ctx.state.error);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ctx.state.error, fragment), ctx.state.error);
  TEST_ASSERT_NULL(built.def);
  c4_state_free(&ctx.state);
}

void test_sync_rejects_a_malformed_l1_account(void) {
  ssz_builder_t req        = ssz_builder_for_type(ETH_SSZ_VERIFY_REQUEST);
  uint8_t       version[4] = {1, 0, 0, 0};
  uint8_t       none       = 0;
  ssz_add_bytes(&req, "version", bytes(version, 4));
  ssz_add_bytes(&req, "data", bytes(&none, 1));
  ssz_add_bytes(&req, "proof", bytes(&none, 1));
  ssz_add_bytes(&req, "sync_data", bytes(&none, 1));
  ssz_ob_t   empty = ssz_builder_to_bytes(&req);
  c4_state_t state = {0};
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(empty, true, &state), state.error ? state.error : "empty request");
  c4_state_free(&state);
  expect_answer_error(empty.bytes, "L1 prover did not return an account proof for the batch poster");

  uint8_t slot[32] = {0};
  uint8_t wrong[32];
  memset(wrong, 0x11, sizeof(wrong));
  arb_batch_poster_storage_key(KNOWN_POSTER, slot);
  expect_answer_error(l1_answer(INBOX, slot, 1, false, 0), "L1 prover did not prove the batch poster with a clProof");
  expect_answer_error(l1_answer(KNOWN_POSTER, slot, 1, true, 0), "L1 account proof is not the sequencer inbox");
  expect_answer_error(l1_answer(INBOX, slot, 0, true, 0), "batch poster proof must contain one storage entry");
  expect_answer_error(l1_answer(INBOX, slot, 2, true, 0), "batch poster proof must contain one storage entry");
  expect_answer_error(l1_answer(INBOX, wrong, 1, true, 0), "batch poster storage key does not match the signer");
  expect_answer_error(l1_answer(INBOX, slot, 1, true, 2), "L1 prover returned a nested batch poster sync");
}

static void expect_built_sync(int kind, bool none_sync, const char* sync_name) {
  uint8_t slot[32] = {0};
  arb_batch_poster_storage_key(KNOWN_POSTER, slot);
  prover_ctx_t    ctx = poster_ctx(C4_CHAIN_ARB_ONE);
  data_request_t* req = queued_request(&ctx);
  req->response       = l1_answer(INBOX, slot, 1, true, kind);
  ssz_builder_t built = {0};
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, arb_batch_poster_sync(&ctx, &built), ctx.state.error ? ctx.state.error : "sync");
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, arb_batch_poster_prepare(&ctx));
  ssz_ob_t   sync  = ssz_builder_to_bytes(&built);
  c4_state_t state = {0};
  TEST_ASSERT_TRUE_MESSAGE(ssz_is_valid(sync, true, &state), state.error ? state.error : "built sync");
  c4_state_free(&state);
  TEST_ASSERT_EQUAL_STRING(ETH_SSZ_NAME_ARB_SIGNER_SYNC_DATA, sync.def->name);
  TEST_ASSERT_EQUAL_MEMORY(KNOWN_POSTER, ssz_get(&sync, "signer").bytes.data, 20);
  ssz_ob_t account = ssz_get(&sync, "account");
  TEST_ASSERT_EQUAL_MEMORY(INBOX, ssz_get(&account, "address").bytes.data, 20);
  TEST_ASSERT_EQUAL_STRING(ETH_SSZ_NAME_CL_PROOF, ssz_get(&account, "elProof").def->name);
  ssz_ob_t stored = ssz_get(&account, "storageProof");
  ssz_ob_t entry  = ssz_at(stored, 0);
  TEST_ASSERT_EQUAL_UINT32(1, ssz_len(stored));
  TEST_ASSERT_EQUAL_MEMORY(slot, ssz_get(&entry, "key").bytes.data, 32);
  ssz_ob_t inner = ssz_get(&sync, "sync_data");
  if (none_sync)
    TEST_ASSERT_EQUAL_INT(SSZ_TYPE_NONE, inner.def->type);
  else
    TEST_ASSERT_EQUAL_STRING(sync_name, inner.def->name);
  safe_free(sync.bytes.data);
  c4_state_free(&ctx.state);
}

void test_sync_builds_signer_container(void) {
  expect_built_sync(0, true, NULL);
  expect_built_sync(1, false, ETH_SSZ_NAME_LC_SYNC_DATA);
}

void test_sync_hook_owns_the_section(void) {
  prover_ctx_t     ctx    = {0};
  syncdata_state_t sync   = {0};
  ssz_builder_t    built  = {0};
  uint8_t          marker = 0x5a;
  ctx.chain_id            = C4_CHAIN_ARB_ONE;
  sync.l1_sync_data.def   = eth_ssz_verification_type(ETH_SSZ_VERIFY_LC_SYNCDATA);
  sync.l1_sync_data.bytes = bytes(&marker, 1);

  hook_calls = 0;
  c4_register_sync_data_prover(C4_CHAIN_TYPE_ARBITRUM, counting_hook);
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, c4_get_syncdata_proof(&ctx, &sync, &built));
  TEST_ASSERT_EQUAL_INT(1, hook_calls);
  TEST_ASSERT_EQUAL_PTR(sync.l1_sync_data.def, built.def);
  TEST_ASSERT_EQUAL_UINT32(1, built.fixed.data.len);
  TEST_ASSERT_EQUAL_HEX8(0x5a, built.fixed.data.data[0]);
  ssz_builder_free(&built);
  memset(&built, 0, sizeof(built));

  c4_register_sync_data_prover(C4_CHAIN_TYPE_ARBITRUM, def_hook);
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, c4_get_syncdata_proof(&ctx, &sync, &built));
  TEST_ASSERT_EQUAL_PTR(eth_ssz_verification_type(ETH_SSZ_VERIFY_ARB_SIGNER_SYNC), built.def);
  TEST_ASSERT_EQUAL_UINT32(0, built.fixed.data.len);
  TEST_ASSERT_NULL(ctx.state.requests);
  ssz_builder_free(&built);
  memset(&built, 0, sizeof(built));

  c4_register_sync_data_prover(C4_CHAIN_TYPE_ARBITRUM, pending_hook);
  TEST_ASSERT_EQUAL_INT(C4_PENDING, c4_get_syncdata_proof(&ctx, &sync, &built));
  TEST_ASSERT_NULL(built.def);
  TEST_ASSERT_NULL(ctx.state.requests);

  c4_register_sync_data_prover(C4_CHAIN_TYPE_ARBITRUM, error_hook);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, c4_get_syncdata_proof(&ctx, &sync, &built));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "hook failed"));
  c4_state_free(&ctx.state);

  c4_register_sync_data_prover((chain_type_t) 100, counting_hook);
  TEST_ASSERT_NULL(c4_sync_data_prover_fn((chain_type_t) 100));
}

void test_unknown_arbitrum_chain_has_no_inbox(void) {
  ssz_ob_t     sync_ob = blockhash_signer_sync();
  verify_ctx_t ctx     = {0};
  ctx.chain_id         = CHAIN_ID(C4_CHAIN_TYPE_ARBITRUM, 99);
  ctx.sync_data        = sync_ob;
  TEST_ASSERT_TRUE(c4_arb_verify(&ctx));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "no sequencer inbox for this chain"));
  TEST_ASSERT_EQUAL_STRING(ETH_SSZ_NAME_ARB_SIGNER_SYNC_DATA, ctx.sync_data.def->name);
  TEST_ASSERT_EQUAL_UINT64(CHAIN_ID(C4_CHAIN_TYPE_ARBITRUM, 99), ctx.chain_id);
  c4_state_free(&ctx.state);
  safe_free(sync_ob.bytes.data);
}

void test_nested_sync_error_restores_the_outer_object(void) {
  ssz_ob_t      inner = blockhash_signer_sync();
  ssz_builder_t outer = ssz_builder_for_type(ETH_SSZ_VERIFY_ARB_SIGNER_SYNC);
  ssz_add_bytes(&outer, "signer", bytes((uint8_t*) KNOWN_POSTER, 20));
  ssz_add_bytes(&outer, "account", ssz_get(&inner, "account").bytes);
  ssz_add_ob(&outer, "sync_data", inner);
  ssz_ob_t     sync_ob = ssz_builder_to_bytes(&outer);
  verify_ctx_t ctx     = {0};
  ctx.chain_id         = C4_CHAIN_ARB_ONE;
  ctx.sync_data        = sync_ob;
  TEST_ASSERT_TRUE(c4_arb_verify(&ctx));
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "unknown sync_data type"));
  TEST_ASSERT_EQUAL_STRING(ETH_SSZ_NAME_ARB_SIGNER_SYNC_DATA, ctx.sync_data.def->name);
  TEST_ASSERT_EQUAL_UINT64(C4_CHAIN_ARB_ONE, ctx.chain_id);
  c4_state_free(&ctx.state);
  safe_free(inner.bytes.data);
  safe_free(sync_ob.bytes.data);
}

void test_other_sync_is_not_treated_as_a_batch_poster(void) {
  verify_ctx_t ctx = {0};
  ctx.chain_id     = C4_CHAIN_MAINNET;
  TEST_ASSERT_FALSE(c4_arb_verify(&ctx));

  static uint8_t none_marker = 0;
  ctx.chain_id               = C4_CHAIN_ARB_ONE;
  ctx.method                 = "nope";
  ctx.proof.def              = &ssz_none;
  ctx.proof.bytes            = bytes(&none_marker, 0);
  ctx.sync_data.def          = &ssz_none;
  ctx.sync_data.bytes        = bytes(&none_marker, 0);
  ctx.data.def               = &ssz_none;
  ctx.data.bytes             = bytes(&none_marker, 0);
  TEST_ASSERT_TRUE(c4_arb_verify(&ctx));
  TEST_ASSERT_FALSE(ctx.success);
  TEST_ASSERT_NOT_NULL(strstr(ctx.state.error, "proof is not a supported proof type"));
  TEST_ASSERT_NULL(strstr(ctx.state.error, "batch poster"));
  c4_state_free(&ctx.state);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_batch_poster_storage_key);
  RUN_TEST(test_batch_poster_cache_roundtrip);
  RUN_TEST(test_batch_poster_l1_number_is_monotonic);
  RUN_TEST(test_append_sends_batch_poster_for_one_day);
  RUN_TEST(test_signer_sync_is_union_index_3);
  RUN_TEST(test_blockhash_signer_sync_restores_the_outer_object);
  RUN_TEST(test_sequencer_inbox_is_only_arbitrum_one);
  RUN_TEST(test_cache_rejects_null_and_a_short_record);
  RUN_TEST(test_append_sends_a_future_or_exact_timestamp);
  RUN_TEST(test_storage_key_ignores_null);
  RUN_TEST(test_feed_recover_signer_accepts_both_v_ranges);
  RUN_TEST(test_flag_off_does_not_queue);
  RUN_TEST(test_missing_poster_and_unknown_chain_fail_before_a_request);
  RUN_TEST(test_l1_request_targets_mainnet_and_omits_the_cached_poster);
  RUN_TEST(test_failed_l1_answer_stops_prepare);
  RUN_TEST(test_sync_rejects_a_malformed_l1_account);
  RUN_TEST(test_sync_builds_signer_container);
  RUN_TEST(test_sync_hook_owns_the_section);
  RUN_TEST(test_unknown_arbitrum_chain_has_no_inbox);
  RUN_TEST(test_nested_sync_error_restores_the_outer_object);
  RUN_TEST(test_other_sync_is_not_treated_as_a_batch_poster);
  return UNITY_END();
}

#endif
