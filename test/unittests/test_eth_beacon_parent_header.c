/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * Parent-to-child beacon header lookup must not depend on
 * GET /eth/v1/beacon/headers?parent_root= (unimplemented in Nimbus,
 * status-im/nimbus-eth2#7305). The prover fetches the parent by root
 * and scans subsequent slots via ?slot=.
 */

#include "beacon.h"
#include "bytes.h"
#include "prover.h"
#include "state.h"
#include "unity.h"
#include <stdlib.h>
#include <string.h>

static const char* k_empty_headers = "{\"data\":[]}";

void setUp(void) {}
void tearDown(void) {}

static void hex32(const uint8_t* b, char out[65]) {
  static const char* h = "0123456789abcdef";
  for (int i = 0; i < 32; i++) {
    out[i * 2]     = h[b[i] >> 4];
    out[i * 2 + 1] = h[b[i] & 0xf];
  }
  out[64] = 0;
}

static char* header_by_id_json(const uint8_t* root, uint64_t slot, const uint8_t* parent) {
  char rh[65], ph[65];
  hex32(root, rh);
  hex32(parent, ph);
  return bprintf(NULL,
                 "{\"data\":{\"root\":\"0x%s\",\"canonical\":true,\"header\":{\"message\":{"
                 "\"slot\":\"%l\",\"proposer_index\":\"1\",\"parent_root\":\"0x%s\","
                 "\"state_root\":\"0x%s\",\"body_root\":\"0x%s\"},\"signature\":\"0x%s\"}}}",
                 rh, slot, ph, rh, rh, rh);
}

static char* header_list_json(const uint8_t* root, uint64_t slot, const uint8_t* parent) {
  char rh[65], ph[65];
  hex32(root, rh);
  hex32(parent, ph);
  return bprintf(NULL,
                 "{\"data\":[{\"root\":\"0x%s\",\"canonical\":true,\"header\":{\"message\":{"
                 "\"slot\":\"%l\",\"proposer_index\":\"1\",\"parent_root\":\"0x%s\","
                 "\"state_root\":\"0x%s\",\"body_root\":\"0x%s\"},\"signature\":\"0x%s\"}}]}",
                 rh, slot, ph, rh, rh, rh);
}

static void fulfill(data_request_t* req, char* json) {
  TEST_ASSERT_NOT_NULL(json);
  req->response = bytes((uint8_t*) json, (uint32_t) strlen(json));
}

static void fulfill_const(data_request_t* req, const char* json) {
  req->response = bytes((uint8_t*) strdup(json), (uint32_t) strlen(json));
}

static int pending_count(c4_state_t* state) {
  int             n   = 0;
  data_request_t* req = state->requests;
  while (req) {
    if (c4_state_is_pending(req)) n++;
    req = req->next;
  }
  return n;
}

static data_request_t* first_pending(c4_state_t* state) {
  return c4_state_get_pending_request(state);
}

static const char k_slot_prefix[] = "eth/v1/beacon/headers?slot=";

static bool parse_slot_query(const char* url, uint64_t* slot) {
  const char* p;
  if (!url || !slot) return false;
  p = strstr(url, k_slot_prefix);
  if (!p) return false;
  *slot = (uint64_t) strtoull(p + (sizeof(k_slot_prefix) - 1), NULL, 10);
  return true;
}

static bool is_parent_by_root(const char* url, const char* parent_hex) {
  return url && strstr(url, "eth/v1/beacon/headers/0x") && strstr(url, parent_hex);
}

void test_parent_lookup_does_not_use_parent_root_query(void) {
  bytes32_t parent = {0};
  memset(parent, 0xab, 32);

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  TEST_ASSERT_NOT_NULL(ctx);

  ssz_ob_t    sig_block = {0}, data_block = {0};
  bytes32_t   data_root_out = {0};
  c4_status_t st            = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
  TEST_ASSERT_EQUAL(C4_PENDING, st);

  data_request_t* req = first_pending(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_NOT_NULL(req->url);
  TEST_ASSERT_NOT_NULL(strstr(req->url, "eth/v1/beacon/headers/0x"));
  TEST_ASSERT_NULL(strstr(req->url, "parent_root"));

  c4_prover_free(ctx);
}

void test_parent_lookup_scans_empty_slots_then_matches(void) {
  bytes32_t parent = {0};
  bytes32_t child  = {0};
  bytes32_t dummy  = {0};
  memset(parent, 0xab, 32);
  memset(child, 0xcd, 32);
  memset(dummy, 0x11, 32);

  char parent_hex[65];
  hex32(parent, parent_hex);

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  TEST_ASSERT_NOT_NULL(ctx);

  ssz_ob_t    sig_block = {0}, data_block = {0};
  bytes32_t   data_root_out   = {0};
  c4_status_t st              = C4_PENDING;
  int         loops           = 8;
  int         saw_block_fetch = 0;

  while (st == C4_PENDING && loops-- > 0 && !saw_block_fetch) {
    st = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
    if (st != C4_PENDING) break;

    for (data_request_t* req = ctx->state.requests; req; req = req->next) {
      if (!c4_state_is_pending(req) || !req->url) continue;

      if (strstr(req->url, "parent_root")) {
        c4_prover_free(ctx);
        TEST_FAIL_MESSAGE("must not query headers?parent_root=");
      }

      if (strstr(req->url, "eth/v1/beacon/headers/0x") && strstr(req->url, parent_hex)) {
        fulfill(req, header_by_id_json(parent, 100, dummy));
        continue;
      }
      if (strstr(req->url, "eth/v1/beacon/headers?slot=101")) {
        fulfill_const(req, k_empty_headers);
        continue;
      }
      if (strstr(req->url, "eth/v1/beacon/headers?slot=102")) {
        fulfill(req, header_list_json(child, 102, parent));
        continue;
      }
      if (strstr(req->url, "eth/v1/beacon/headers?slot=")) {
        fulfill_const(req, k_empty_headers);
        continue;
      }
      if (strstr(req->url, "eth/v2/beacon/blocks/")) {
        saw_block_fetch = 1;
        break;
      }

      c4_prover_free(ctx);
      TEST_FAIL_MESSAGE(req->url);
    }
  }

  TEST_ASSERT_TRUE_MESSAGE(saw_block_fetch, "did not reach block fetch after slot scan");
  c4_prover_free(ctx);
}

void test_parent_lookup_not_found_when_no_child_in_window(void) {
  bytes32_t parent = {0};
  bytes32_t dummy  = {0};
  memset(parent, 0xab, 32);
  memset(dummy, 0x11, 32);

  char parent_hex[65];
  hex32(parent, parent_hex);

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  TEST_ASSERT_NOT_NULL(ctx);

  ssz_ob_t    sig_block = {0}, data_block = {0};
  bytes32_t   data_root_out = {0};
  c4_status_t st            = C4_PENDING;
  int         loops         = 16;

  while (st == C4_PENDING && loops-- > 0) {
    st = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
    if (st != C4_PENDING) break;

    TEST_ASSERT_TRUE(pending_count(&ctx->state) > 0);

    for (data_request_t* req = ctx->state.requests; req; req = req->next) {
      if (!c4_state_is_pending(req) || !req->url) continue;

      if (strstr(req->url, "eth/v1/beacon/headers/0x") && strstr(req->url, parent_hex)) {
        fulfill(req, header_by_id_json(parent, 100, dummy));
        continue;
      }
      if (strstr(req->url, "eth/v1/beacon/headers?slot=")) {
        fulfill_const(req, k_empty_headers);
        continue;
      }

      TEST_FAIL_MESSAGE(req->url);
    }
  }

  TEST_ASSERT_EQUAL(C4_ERROR, st);
  TEST_ASSERT_NOT_NULL(ctx->state.error);
  TEST_ASSERT_NOT_NULL(strstr(ctx->state.error, "not been signed yet"));
  c4_prover_free(ctx);
}

void test_parent_lookup_malformed_parent_json(void) {
  bytes32_t parent = {0};
  memset(parent, 0xab, 32);

  char parent_hex[65];
  hex32(parent, parent_hex);

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  TEST_ASSERT_NOT_NULL(ctx);

  ssz_ob_t    sig_block = {0}, data_block = {0};
  bytes32_t   data_root_out = {0};
  c4_status_t st            = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
  TEST_ASSERT_EQUAL(C4_PENDING, st);

  data_request_t* req = first_pending(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_TRUE(is_parent_by_root(req->url, parent_hex));
  fulfill_const(req, "{\"data\":");

  st = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
  TEST_ASSERT_EQUAL(C4_ERROR, st);
  TEST_ASSERT_NOT_NULL(ctx->state.error);
  TEST_ASSERT_NOT_NULL(strstr(ctx->state.error, "Invalid JSON"));
  c4_prover_free(ctx);
}

void test_parent_lookup_parent_header_not_found(void) {
  bytes32_t parent = {0};
  memset(parent, 0xab, 32);

  char parent_hex[65];
  hex32(parent, parent_hex);

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  TEST_ASSERT_NOT_NULL(ctx);

  ssz_ob_t    sig_block = {0}, data_block = {0};
  bytes32_t   data_root_out = {0};
  c4_status_t st            = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
  TEST_ASSERT_EQUAL(C4_PENDING, st);

  data_request_t* req = first_pending(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_TRUE(is_parent_by_root(req->url, parent_hex));
  fulfill_const(req, k_empty_headers);

  st = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
  TEST_ASSERT_EQUAL(C4_ERROR, st);
  TEST_ASSERT_NOT_NULL(ctx->state.error);
  TEST_ASSERT_NOT_NULL(strstr(ctx->state.error, "Parent beacon header not found"));
  c4_prover_free(ctx);
}

void test_parent_lookup_skips_slot_with_other_parent_root(void) {
  bytes32_t parent = {0};
  bytes32_t child  = {0};
  bytes32_t other  = {0};
  bytes32_t dummy  = {0};
  memset(parent, 0xab, 32);
  memset(child, 0xcd, 32);
  memset(other, 0xee, 32);
  memset(dummy, 0x11, 32);

  char parent_hex[65], child_hex[65], other_hex[65];
  hex32(parent, parent_hex);
  hex32(child, child_hex);
  hex32(other, other_hex);

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  TEST_ASSERT_NOT_NULL(ctx);

  ssz_ob_t    sig_block       = {0}, data_block = {0};
  bytes32_t   data_root_out   = {0};
  c4_status_t st              = C4_PENDING;
  int         loops           = 8;
  int         saw_block_fetch = 0;

  while (st == C4_PENDING && loops-- > 0 && !saw_block_fetch) {
    st = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
    if (st != C4_PENDING) break;

    for (data_request_t* req = ctx->state.requests; req; req = req->next) {
      uint64_t slot = 0;
      if (!c4_state_is_pending(req) || !req->url) continue;

      if (strstr(req->url, "parent_root")) {
        c4_prover_free(ctx);
        TEST_FAIL_MESSAGE("must not query headers?parent_root=");
      }
      if (is_parent_by_root(req->url, parent_hex)) {
        fulfill(req, header_by_id_json(parent, 100, dummy));
        continue;
      }
      if (parse_slot_query(req->url, &slot) && slot == 101) {
        fulfill(req, header_list_json(other, 101, dummy));
        continue;
      }
      if (parse_slot_query(req->url, &slot) && slot == 102) {
        fulfill(req, header_list_json(child, 102, parent));
        continue;
      }
      if (parse_slot_query(req->url, &slot)) {
        fulfill_const(req, k_empty_headers);
        continue;
      }
      if (strstr(req->url, "eth/v2/beacon/blocks/")) {
        TEST_ASSERT_NULL_MESSAGE(strstr(req->url, other_hex), req->url);
        // Sign-block is the matching child; data-block is the parent root.
        if (strstr(req->url, child_hex))
          saw_block_fetch = 1;
        else
          TEST_ASSERT_NOT_NULL_MESSAGE(strstr(req->url, parent_hex), req->url);
        continue;
      }

      c4_prover_free(ctx);
      TEST_FAIL_MESSAGE(req->url);
    }
  }

  TEST_ASSERT_TRUE_MESSAGE(saw_block_fetch, "mismatching slot header must be skipped");
  c4_prover_free(ctx);
}

void test_parent_lookup_matches_last_slot_in_window(void) {
  bytes32_t parent = {0};
  bytes32_t child  = {0};
  bytes32_t dummy  = {0};
  memset(parent, 0xab, 32);
  memset(child, 0xcd, 32);
  memset(dummy, 0x11, 32);

  char parent_hex[65], child_hex[65];
  hex32(parent, parent_hex);
  hex32(child, child_hex);

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  TEST_ASSERT_NOT_NULL(ctx);

  ssz_ob_t    sig_block       = {0}, data_block = {0};
  bytes32_t   data_root_out   = {0};
  c4_status_t st              = C4_PENDING;
  int         loops           = 16;
  int         saw_slot_132    = 0;
  int         saw_block_fetch = 0;

  while (st == C4_PENDING && loops-- > 0 && !saw_block_fetch) {
    st = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
    if (st != C4_PENDING) break;

    for (data_request_t* req = ctx->state.requests; req; req = req->next) {
      uint64_t slot = 0;
      if (!c4_state_is_pending(req) || !req->url) continue;

      if (strstr(req->url, "parent_root")) {
        c4_prover_free(ctx);
        TEST_FAIL_MESSAGE("must not query headers?parent_root=");
      }
      if (is_parent_by_root(req->url, parent_hex)) {
        fulfill(req, header_by_id_json(parent, 100, dummy));
        continue;
      }
      if (parse_slot_query(req->url, &slot)) {
        if (slot < 101 || slot > 132) {
          c4_prover_free(ctx);
          TEST_FAIL_MESSAGE(req->url);
        }
        if (slot == 132) {
          saw_slot_132 = 1;
          fulfill(req, header_list_json(child, 132, parent));
        }
        else {
          fulfill_const(req, k_empty_headers);
        }
        continue;
      }
      if (strstr(req->url, "eth/v2/beacon/blocks/")) {
        if (strstr(req->url, child_hex))
          saw_block_fetch = 1;
        else
          TEST_ASSERT_NOT_NULL_MESSAGE(strstr(req->url, parent_hex), req->url);
        continue;
      }

      c4_prover_free(ctx);
      TEST_FAIL_MESSAGE(req->url);
    }
  }

  TEST_ASSERT_TRUE_MESSAGE(saw_slot_132, "must scan through slot parent+32");
  TEST_ASSERT_TRUE_MESSAGE(saw_block_fetch, "match at last window slot must fetch the child block");
  c4_prover_free(ctx);
}

void test_parent_lookup_first_window_is_parallel(void) {
  bytes32_t parent = {0};
  bytes32_t dummy  = {0};
  memset(parent, 0xab, 32);
  memset(dummy, 0x11, 32);

  char parent_hex[65];
  hex32(parent, parent_hex);

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  TEST_ASSERT_NOT_NULL(ctx);

  ssz_ob_t    sig_block     = {0}, data_block = {0};
  bytes32_t   data_root_out = {0};
  c4_status_t st            = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
  TEST_ASSERT_EQUAL(C4_PENDING, st);

  data_request_t* req = first_pending(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_TRUE(is_parent_by_root(req->url, parent_hex));
  fulfill(req, header_by_id_json(parent, 100, dummy));

  st = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
  TEST_ASSERT_EQUAL(C4_PENDING, st);
  TEST_ASSERT_EQUAL_INT(8, pending_count(&ctx->state));

  uint32_t seen = 0;
  for (req = ctx->state.requests; req; req = req->next) {
    uint64_t slot = 0;
    if (!c4_state_is_pending(req) || !req->url) continue;
    TEST_ASSERT_TRUE_MESSAGE(parse_slot_query(req->url, &slot), req->url);
    TEST_ASSERT_TRUE_MESSAGE(slot >= 101 && slot <= 108, req->url);
    TEST_ASSERT_EQUAL_UINT32(6, req->ttl);
    uint32_t bit = 1u << (unsigned) (slot - 101);
    TEST_ASSERT_EQUAL_UINT32(0, seen & bit);
    seen |= bit;
  }
  TEST_ASSERT_EQUAL_HEX32(0xff, seen);
  c4_prover_free(ctx);
}

void test_parent_lookup_prefers_canonical_child_in_slot_array(void) {
  bytes32_t parent = {0};
  bytes32_t child  = {0};
  bytes32_t fork   = {0};
  bytes32_t dummy  = {0};
  memset(parent, 0xab, 32);
  memset(child, 0xcd, 32);
  memset(fork, 0xee, 32);
  memset(dummy, 0x11, 32);

  char parent_hex[65], child_hex[65], fork_hex[65];
  hex32(parent, parent_hex);
  hex32(child, child_hex);
  hex32(fork, fork_hex);

  char* slot_json = bprintf(NULL,
                            "{\"data\":["
                            "{\"root\":\"0x%s\",\"canonical\":false,\"header\":{\"message\":{"
                            "\"slot\":\"101\",\"proposer_index\":\"1\",\"parent_root\":\"0x%s\","
                            "\"state_root\":\"0x%s\",\"body_root\":\"0x%s\"},\"signature\":\"0x%s\"}},"
                            "{\"root\":\"0x%s\",\"canonical\":true,\"header\":{\"message\":{"
                            "\"slot\":\"101\",\"proposer_index\":\"1\",\"parent_root\":\"0x%s\","
                            "\"state_root\":\"0x%s\",\"body_root\":\"0x%s\"},\"signature\":\"0x%s\"}}"
                            "]}",
                            fork_hex, parent_hex, fork_hex, fork_hex, fork_hex,
                            child_hex, parent_hex, child_hex, child_hex, child_hex);

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  TEST_ASSERT_NOT_NULL(ctx);

  ssz_ob_t    sig_block       = {0}, data_block = {0};
  bytes32_t   data_root_out   = {0};
  c4_status_t st              = C4_PENDING;
  int         loops           = 8;
  int         saw_block_fetch = 0;

  while (st == C4_PENDING && loops-- > 0 && !saw_block_fetch) {
    st = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
    if (st != C4_PENDING) break;

    for (data_request_t* req = ctx->state.requests; req; req = req->next) {
      if (!c4_state_is_pending(req) || !req->url) continue;
      if (is_parent_by_root(req->url, parent_hex)) {
        fulfill(req, header_by_id_json(parent, 100, dummy));
        continue;
      }
      if (strstr(req->url, "eth/v1/beacon/headers?slot=101")) {
        fulfill(req, slot_json);
        slot_json = NULL;
        continue;
      }
      if (strstr(req->url, "eth/v1/beacon/headers?slot=")) {
        fulfill_const(req, k_empty_headers);
        continue;
      }
      if (strstr(req->url, "eth/v2/beacon/blocks/")) {
        TEST_ASSERT_NULL_MESSAGE(strstr(req->url, fork_hex), req->url);
        if (strstr(req->url, child_hex))
          saw_block_fetch = 1;
        else
          TEST_ASSERT_NOT_NULL_MESSAGE(strstr(req->url, parent_hex), req->url);
        continue;
      }
      c4_prover_free(ctx);
      TEST_FAIL_MESSAGE(req->url);
    }
  }

  TEST_ASSERT_TRUE_MESSAGE(saw_block_fetch, "canonical child in data[] must win over a fork header");
  if (slot_json) safe_free(slot_json);
  c4_prover_free(ctx);
}

void test_parent_lookup_parent_root_mismatch(void) {
  bytes32_t parent = {0};
  bytes32_t other  = {0};
  memset(parent, 0xab, 32);
  memset(other, 0x11, 32);

  char parent_hex[65];
  hex32(parent, parent_hex);

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  TEST_ASSERT_NOT_NULL(ctx);

  ssz_ob_t    sig_block = {0}, data_block = {0};
  bytes32_t   data_root_out = {0};
  c4_status_t st            = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
  TEST_ASSERT_EQUAL(C4_PENDING, st);

  data_request_t* req = first_pending(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_TRUE(is_parent_by_root(req->url, parent_hex));
  fulfill(req, header_by_id_json(other, 100, other));

  st = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
  TEST_ASSERT_EQUAL(C4_ERROR, st);
  TEST_ASSERT_NOT_NULL(ctx->state.error);
  TEST_ASSERT_NOT_NULL(strstr(ctx->state.error, "root mismatch"));
  c4_prover_free(ctx);
}

void test_parent_lookup_parent_missing_slot(void) {
  bytes32_t parent = {0};
  memset(parent, 0xab, 32);

  char parent_hex[65];
  hex32(parent, parent_hex);

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  TEST_ASSERT_NOT_NULL(ctx);

  ssz_ob_t    sig_block = {0}, data_block = {0};
  bytes32_t   data_root_out = {0};
  c4_status_t st            = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
  TEST_ASSERT_EQUAL(C4_PENDING, st);

  data_request_t* req = first_pending(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_TRUE(is_parent_by_root(req->url, parent_hex));
  fulfill(req, bprintf(NULL,
                       "{\"data\":{\"root\":\"0x%s\",\"canonical\":true,\"header\":{\"message\":{"
                       "\"proposer_index\":\"1\",\"parent_root\":\"0x%s\",\"state_root\":\"0x%s\","
                       "\"body_root\":\"0x%s\"},\"signature\":\"0x%s\"}}}",
                       parent_hex, parent_hex, parent_hex, parent_hex, parent_hex));

  st = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
  TEST_ASSERT_EQUAL(C4_ERROR, st);
  TEST_ASSERT_NOT_NULL(ctx->state.error);
  TEST_ASSERT_NOT_NULL(strstr(ctx->state.error, "missing slot"));
  c4_prover_free(ctx);
}

void test_parent_lookup_rejects_short_child_root(void) {
  bytes32_t parent = {0};
  bytes32_t dummy  = {0};
  memset(parent, 0xab, 32);
  memset(dummy, 0x11, 32);

  char parent_hex[65];
  hex32(parent, parent_hex);

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  TEST_ASSERT_NOT_NULL(ctx);

  ssz_ob_t    sig_block = {0}, data_block = {0};
  bytes32_t   data_root_out = {0};
  c4_status_t st            = C4_PENDING;
  int         loops         = 8;

  while (st == C4_PENDING && loops-- > 0) {
    st = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
    if (st != C4_PENDING) break;

    for (data_request_t* req = ctx->state.requests; req; req = req->next) {
      if (!c4_state_is_pending(req) || !req->url) continue;
      if (is_parent_by_root(req->url, parent_hex)) {
        fulfill(req, header_by_id_json(parent, 100, dummy));
        continue;
      }
      if (strstr(req->url, "eth/v1/beacon/headers?slot=101")) {
        fulfill(req, bprintf(NULL,
                             "{\"data\":[{\"root\":\"0xaa\",\"canonical\":true,\"header\":{\"message\":{"
                             "\"slot\":\"101\",\"proposer_index\":\"1\",\"parent_root\":\"0x%s\","
                             "\"state_root\":\"0x%s\",\"body_root\":\"0x%s\"},\"signature\":\"0x%s\"}}]}",
                             parent_hex, parent_hex, parent_hex, parent_hex));
        continue;
      }
      if (strstr(req->url, "eth/v1/beacon/headers?slot=")) {
        fulfill_const(req, k_empty_headers);
        continue;
      }
      c4_prover_free(ctx);
      TEST_FAIL_MESSAGE(req->url);
    }
  }

  TEST_ASSERT_EQUAL(C4_ERROR, st);
  TEST_ASSERT_NOT_NULL(ctx->state.error);
  TEST_ASSERT_NOT_NULL(strstr(ctx->state.error, "Invalid beacon header root"));
  c4_prover_free(ctx);
}

void test_parent_lookup_slot_error_aborts_window(void) {
  bytes32_t parent = {0};
  bytes32_t dummy  = {0};
  memset(parent, 0xab, 32);
  memset(dummy, 0x11, 32);

  char parent_hex[65];
  hex32(parent, parent_hex);

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  TEST_ASSERT_NOT_NULL(ctx);

  ssz_ob_t    sig_block     = {0}, data_block = {0};
  bytes32_t   data_root_out = {0};
  c4_status_t st            = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
  TEST_ASSERT_EQUAL(C4_PENDING, st);

  data_request_t* req = first_pending(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);
  fulfill(req, header_by_id_json(parent, 100, dummy));

  st = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
  TEST_ASSERT_EQUAL(C4_PENDING, st);

  int marked = 0;
  for (req = ctx->state.requests; req; req = req->next) {
    if (!c4_state_is_pending(req) || !req->url) continue;
    if (!strstr(req->url, k_slot_prefix)) continue;
    if (!marked) {
      req->error = strdup("HTTP 404");
      marked     = 1;
    }
    else {
      fulfill_const(req, k_empty_headers);
    }
  }
  TEST_ASSERT_TRUE(marked);

  st = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
  TEST_ASSERT_EQUAL(C4_ERROR, st);
  TEST_ASSERT_NOT_NULL(ctx->state.error);
  TEST_ASSERT_NOT_NULL(strstr(ctx->state.error, "HTTP 404"));
  c4_prover_free(ctx);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_parent_lookup_does_not_use_parent_root_query);
  RUN_TEST(test_parent_lookup_scans_empty_slots_then_matches);
  RUN_TEST(test_parent_lookup_not_found_when_no_child_in_window);
  RUN_TEST(test_parent_lookup_malformed_parent_json);
  RUN_TEST(test_parent_lookup_parent_header_not_found);
  RUN_TEST(test_parent_lookup_skips_slot_with_other_parent_root);
  RUN_TEST(test_parent_lookup_matches_last_slot_in_window);
  RUN_TEST(test_parent_lookup_first_window_is_parallel);
  RUN_TEST(test_parent_lookup_slot_error_aborts_window);
  RUN_TEST(test_parent_lookup_prefers_canonical_child_in_slot_array);
  RUN_TEST(test_parent_lookup_parent_root_mismatch);
  RUN_TEST(test_parent_lookup_parent_missing_slot);
  RUN_TEST(test_parent_lookup_rejects_short_child_root);
  return UNITY_END();
}
