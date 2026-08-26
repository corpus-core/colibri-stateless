/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * Parent-to-child beacon header lookup:
 * - default: GET /eth/v1/beacon/headers?parent_root=
 * - C4_PROVER_FLAG_NIMBUS: fetch parent by root and scan ?slot=
 *   (Nimbus does not implement parent_root=, status-im/nimbus-eth2#7305).
 */

#include "beacon.h"
#include "beacon_types.h"
#include "bytes.h"
#include "chains.h"
#include "historic_proof.h"
#include "prover.h"
#include "state.h"
#include "unity.h"
#include <stdio.h>
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

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, C4_PROVER_FLAG_NIMBUS);
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

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, C4_PROVER_FLAG_NIMBUS);
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

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, C4_PROVER_FLAG_NIMBUS);
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

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, C4_PROVER_FLAG_NIMBUS);
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

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, C4_PROVER_FLAG_NIMBUS);
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

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, C4_PROVER_FLAG_NIMBUS);
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

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, C4_PROVER_FLAG_NIMBUS);
  TEST_ASSERT_NOT_NULL(ctx);

  ssz_ob_t    sig_block = {0}, data_block = {0};
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

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, C4_PROVER_FLAG_NIMBUS);
  TEST_ASSERT_NOT_NULL(ctx);

  ssz_ob_t    sig_block = {0}, data_block = {0};
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

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, C4_PROVER_FLAG_NIMBUS);
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

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, C4_PROVER_FLAG_NIMBUS);
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

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, C4_PROVER_FLAG_NIMBUS);
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

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, C4_PROVER_FLAG_NIMBUS);
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

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, C4_PROVER_FLAG_NIMBUS);
  TEST_ASSERT_NOT_NULL(ctx);

  ssz_ob_t    sig_block = {0}, data_block = {0};
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

void test_parent_lookup_default_uses_parent_root_query(void) {
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
  TEST_ASSERT_NOT_NULL(req->url);
  TEST_ASSERT_NOT_NULL(strstr(req->url, "eth/v1/beacon/headers?parent_root=0x"));
  TEST_ASSERT_NOT_NULL(strstr(req->url, parent_hex));
  TEST_ASSERT_EQUAL_UINT32(BEACON_SUPPORTS_PARENT_ROOT_HEADERS, req->preferred_client_type);
  TEST_ASSERT_EQUAL_UINT32(6, req->ttl);
  c4_prover_free(ctx);
}

void test_parent_lookup_default_empty_means_not_signed(void) {
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
  TEST_ASSERT_NOT_NULL(strstr(req->url, "parent_root="));
  fulfill_const(req, k_empty_headers);

  st = c4_eth_get_signblock_and_parent(ctx, NULL, parent, &sig_block, &data_block, data_root_out);
  TEST_ASSERT_EQUAL(C4_ERROR, st);
  TEST_ASSERT_NOT_NULL(ctx->state.error);
  TEST_ASSERT_NOT_NULL(strstr(ctx->state.error, "not been signed yet"));
  c4_prover_free(ctx);
}

void test_parent_lookup_default_match_fetches_child_block(void) {
  bytes32_t parent = {0};
  bytes32_t child  = {0};
  memset(parent, 0xab, 32);
  memset(child, 0xcd, 32);

  char parent_hex[65], child_hex[65];
  hex32(parent, parent_hex);
  hex32(child, child_hex);

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
      if (strstr(req->url, "headers?parent_root=")) {
        TEST_ASSERT_NOT_NULL(strstr(req->url, parent_hex));
        fulfill(req, header_list_json(child, 101, parent));
        continue;
      }
      if (strstr(req->url, "eth/v2/beacon/blocks/")) {
        TEST_ASSERT_NULL_MESSAGE(strstr(req->url, "headers?slot="), req->url);
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

  TEST_ASSERT_TRUE_MESSAGE(saw_block_fetch, "parent_root query must yield a child block fetch");
  c4_prover_free(ctx);
}

static char* gloas_el_rpc(const uint8_t* hash, const uint8_t* parent_cl, uint64_t slot) {
  char hh[65], ph[65];
  hex32(hash, hh);
  hex32(parent_cl, ph);
  return bprintf(NULL,
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{"
                 "\"number\":\"0x1\",\"hash\":\"0x%s\","
                 "\"parentBeaconBlockRoot\":\"0x%s\",\"slotNumber\":\"0x%lx\""
                 "}}",
                 hh, ph, slot);
}

static int is_eth_get_block_number(data_request_t* req, const char* hex_num) {
  char needle[32];
  if (!req || req->type != C4_DATA_TYPE_ETH_RPC || !req->payload.data) return 0;
  if (!strstr((char*) req->payload.data, "eth_getBlockByNumber")) return 0;
  snprintf(needle, sizeof(needle), "\"%s\"", hex_num);
  return strstr((char*) req->payload.data, needle) != NULL;
}

void test_gloas_by_number_uses_slot_then_parent_root(void) {
  bytes32_t parent_cl = {0};
  bytes32_t el_hash   = {0};
  bytes32_t exec      = {0};
  bytes32_t data      = {0};
  bytes32_t sig       = {0};
  memset(parent_cl, 0xab, 32);
  memset(el_hash, 0x11, 32);
  memset(exec, 0xcd, 32);
  memset(data, 0xee, 32);
  memset(sig, 0xff, 32);

  char exec_hex[65], data_hex[65], sig_hex[65], parent_hex[65];
  hex32(exec, exec_hex);
  hex32(data, data_hex);
  hex32(sig, sig_hex);
  hex32(parent_cl, parent_hex);

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_PLATABERGET, 0);
  TEST_ASSERT_NOT_NULL(ctx);

  eth_block_t beacon_block = {0};
  json_t      block_id     = {.start = "\"0x1\"", .len = 5, .type = JSON_TYPE_STRING};
  c4_status_t st           = C4_PENDING;
  int         loops        = 12;
  int         saw_slot     = 0;
  int         saw_data_pr  = 0;
  int         saw_sig_pr   = 0;
  int         saw_block    = 0;

  while (st == C4_PENDING && loops-- > 0 && !saw_block) {
    st = c4_beacon_get_block_for_eth(ctx, block_id, &beacon_block);
    if (st != C4_PENDING) break;

    for (data_request_t* req = ctx->state.requests; req; req = req->next) {
      if (!c4_state_is_pending(req)) continue;
      if (is_eth_get_block_number(req, "0x1")) {
        TEST_ASSERT_FALSE_MESSAGE(is_eth_get_block_number(req, "0x2"), "Gloas must fetch EL N, not N+1");
        fulfill(req, gloas_el_rpc(el_hash, parent_cl, 100));
        continue;
      }
      if (req->url && strstr(req->url, "eth/v1/beacon/headers?slot=100")) {
        saw_slot = 1;
        fulfill(req, header_list_json(exec, 100, parent_cl));
        continue;
      }
      if (req->url && strstr(req->url, "headers?parent_root=") && strstr(req->url, exec_hex)) {
        saw_data_pr = 1;
        fulfill(req, header_list_json(data, 101, exec));
        continue;
      }
      if (req->url && strstr(req->url, "headers?parent_root=") && strstr(req->url, data_hex)) {
        saw_sig_pr = 1;
        fulfill(req, header_list_json(sig, 102, data));
        continue;
      }
      if (req->url && strstr(req->url, "eth/v2/beacon/blocks/")) {
        saw_block = 1;
        continue;
      }
      if (req->url && strstr(req->url, "headers?slot=") && !strstr(req->url, "slot=100")) {
        c4_prover_free(ctx);
        TEST_FAIL_MESSAGE("Lodestar Gloas path must not slot-scan after the execution slot");
      }
      c4_prover_free(ctx);
      TEST_FAIL_MESSAGE(req->url ? req->url : (char*) req->payload.data);
    }
  }

  TEST_ASSERT_TRUE(saw_slot);
  TEST_ASSERT_TRUE(saw_data_pr);
  TEST_ASSERT_TRUE(saw_sig_pr);
  TEST_ASSERT_TRUE_MESSAGE(saw_block, "Gloas slot + parent_root hops must reach a block fetch");
  c4_prover_free(ctx);
}

static int is_eth_get_block_hash(data_request_t* req, const char* hash_hex) {
  if (!req || req->type != C4_DATA_TYPE_ETH_RPC || !req->payload.data) return 0;
  if (!strstr((char*) req->payload.data, "eth_getBlockByHash")) return 0;
  return strstr((char*) req->payload.data, hash_hex) != NULL;
}

static char* el_rpc_without_slot(const uint8_t* hash, const uint8_t* parent_cl) {
  char hh[65], ph[65];
  hex32(hash, hh);
  hex32(parent_cl, ph);
  return bprintf(NULL,
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{"
                 "\"number\":\"0x1\",\"hash\":\"0x%s\","
                 "\"parentBeaconBlockRoot\":\"0x%s\""
                 "}}",
                 hh, ph);
}

static eth_block_t dummy_block_with_state_root(const uint8_t* state_root, ssz_def_t* header_def, uint8_t* header_bytes) {
  memset(header_bytes, 0, 112);
  memcpy(header_bytes + 48, state_root, 32);
  eth_block_t block = {0};
  block.cl_header   = (ssz_ob_t) {.bytes = {.data = header_bytes, .len = 112}, .def = header_def};
  return block;
}

void test_gloas_by_hash_uses_slot_then_parent_root(void) {
  bytes32_t parent_cl = {0};
  bytes32_t el_hash   = {0};
  bytes32_t exec      = {0};
  bytes32_t data      = {0};
  bytes32_t sig       = {0};
  memset(parent_cl, 0xab, 32);
  memset(el_hash, 0x11, 32);
  memset(exec, 0xcd, 32);
  memset(data, 0xee, 32);
  memset(sig, 0xff, 32);

  char exec_hex[65], data_hex[65], hash_hex[65], hash_json[69];
  hex32(exec, exec_hex);
  hex32(data, data_hex);
  hex32(el_hash, hash_hex);
  snprintf(hash_json, sizeof(hash_json), "\"0x%s\"", hash_hex);
  TEST_ASSERT_EQUAL_UINT32(68, (uint32_t) strlen(hash_json));

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByHash", "[\"0x1\",false]", C4_CHAIN_PLATABERGET, 0);
  TEST_ASSERT_NOT_NULL(ctx);

  eth_block_t beacon_block = {0};
  json_t      block_id     = {.start = hash_json, .len = 68, .type = JSON_TYPE_STRING};
  c4_status_t st           = C4_PENDING;
  int         loops        = 12;
  int         saw_hash     = 0;
  int         saw_slot     = 0;
  int         saw_data_pr  = 0;
  int         saw_sig_pr   = 0;
  int         saw_block    = 0;

  while (st == C4_PENDING && loops-- > 0 && !saw_block) {
    st = c4_beacon_get_block_for_eth(ctx, block_id, &beacon_block);
    if (st != C4_PENDING) break;

    for (data_request_t* req = ctx->state.requests; req; req = req->next) {
      if (!c4_state_is_pending(req)) continue;
      if (is_eth_get_block_number(req, "0x2")) {
        c4_prover_free(ctx);
        TEST_FAIL_MESSAGE("Gloas by hash must not fetch EL N+1");
      }
      if (is_eth_get_block_hash(req, hash_hex)) {
        saw_hash = 1;
        fulfill(req, gloas_el_rpc(el_hash, parent_cl, 100));
        continue;
      }
      if (req->url && strstr(req->url, "eth/v1/beacon/headers?slot=100")) {
        saw_slot = 1;
        fulfill(req, header_list_json(exec, 100, parent_cl));
        continue;
      }
      if (req->url && strstr(req->url, "headers?parent_root=") && strstr(req->url, exec_hex)) {
        saw_data_pr = 1;
        fulfill(req, header_list_json(data, 101, exec));
        continue;
      }
      if (req->url && strstr(req->url, "headers?parent_root=") && strstr(req->url, data_hex)) {
        saw_sig_pr = 1;
        fulfill(req, header_list_json(sig, 102, data));
        continue;
      }
      if (req->url && strstr(req->url, "eth/v2/beacon/blocks/")) {
        saw_block = 1;
        continue;
      }
      c4_prover_free(ctx);
      TEST_FAIL_MESSAGE(req->url ? req->url : (char*) req->payload.data);
    }
  }

  TEST_ASSERT_TRUE(saw_hash);
  TEST_ASSERT_TRUE(saw_slot);
  TEST_ASSERT_TRUE(saw_data_pr);
  TEST_ASSERT_TRUE(saw_sig_pr);
  TEST_ASSERT_TRUE_MESSAGE(saw_block, "Gloas by-hash must hop exec -> data -> sig");
  c4_prover_free(ctx);
}

void test_gloas_without_slot_number_falls_back_to_n_plus_one(void) {
  bytes32_t parent_cl = {0};
  bytes32_t el_hash   = {0};
  memset(parent_cl, 0xab, 32);
  memset(el_hash, 0x11, 32);

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_PLATABERGET, 0);
  TEST_ASSERT_NOT_NULL(ctx);

  eth_block_t beacon_block = {0};
  json_t      block_id     = {.start = "\"0x1\"", .len = 5, .type = JSON_TYPE_STRING};
  c4_status_t st           = C4_PENDING;
  int         loops        = 6;
  int         saw_n        = 0;
  int         saw_n1       = 0;

  while (st == C4_PENDING && loops-- > 0 && !saw_n1) {
    st = c4_beacon_get_block_for_eth(ctx, block_id, &beacon_block);
    if (st != C4_PENDING) break;

    for (data_request_t* req = ctx->state.requests; req; req = req->next) {
      if (!c4_state_is_pending(req)) continue;
      if (is_eth_get_block_number(req, "0x1")) {
        saw_n = 1;
        fulfill(req, el_rpc_without_slot(el_hash, parent_cl));
        continue;
      }
      if (is_eth_get_block_number(req, "0x2")) {
        saw_n1 = 1;
        continue;
      }
      if (req->url && strstr(req->url, "headers?slot=")) {
        c4_prover_free(ctx);
        TEST_FAIL_MESSAGE("missing slotNumber must not enter the Gloas slot path");
      }
      c4_prover_free(ctx);
      TEST_FAIL_MESSAGE(req->url ? req->url : (char*) req->payload.data);
    }
  }

  TEST_ASSERT_TRUE(saw_n);
  TEST_ASSERT_TRUE_MESSAGE(saw_n1, "Gloas-scheduled chain without slotNumber must fall back to EL N+1");
  c4_prover_free(ctx);
}

void test_gloas_execution_slot_wrong_parent_errors(void) {
  bytes32_t parent_cl = {0};
  bytes32_t other     = {0};
  bytes32_t el_hash   = {0};
  bytes32_t exec      = {0};
  memset(parent_cl, 0xab, 32);
  memset(other, 0x99, 32);
  memset(el_hash, 0x11, 32);
  memset(exec, 0xcd, 32);

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_PLATABERGET, 0);
  TEST_ASSERT_NOT_NULL(ctx);

  eth_block_t beacon_block = {0};
  json_t      block_id     = {.start = "\"0x1\"", .len = 5, .type = JSON_TYPE_STRING};
  c4_status_t st           = C4_PENDING;
  int         loops        = 8;

  while (st == C4_PENDING && loops-- > 0) {
    st = c4_beacon_get_block_for_eth(ctx, block_id, &beacon_block);
    if (st != C4_PENDING) break;

    for (data_request_t* req = ctx->state.requests; req; req = req->next) {
      if (!c4_state_is_pending(req)) continue;
      if (is_eth_get_block_number(req, "0x1")) {
        fulfill(req, gloas_el_rpc(el_hash, parent_cl, 100));
        continue;
      }
      if (req->url && strstr(req->url, "eth/v1/beacon/headers?slot=100")) {
        fulfill(req, header_list_json(exec, 100, other));
        continue;
      }
      c4_prover_free(ctx);
      TEST_FAIL_MESSAGE(req->url ? req->url : (char*) req->payload.data);
    }
  }

  TEST_ASSERT_EQUAL(C4_ERROR, st);
  TEST_ASSERT_NOT_NULL(ctx->state.error);
  TEST_ASSERT_NOT_NULL(strstr(ctx->state.error, "No canonical beacon header at that slot"));
  c4_prover_free(ctx);
}

void test_historical_summaries_default_uses_lodestar_url(void) {
  bytes32_t state_root = {0};
  memset(state_root, 0xaa, 32);
  char state_hex[65];
  hex32(state_root, state_hex);

  uint8_t   header_bytes[112];
  ssz_def_t header_def  = SSZ_CONTAINER("BeaconBlockHeader", BEACON_BLOCK_HEADER);
  eth_block_t block     = dummy_block_with_state_root(state_root, &header_def, header_bytes);

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, 0);
  TEST_ASSERT_NOT_NULL(ctx);

  json_t      history = {0};
  c4_status_t st      = c4_test_get_historical_summaries(ctx, &block, &history);
  TEST_ASSERT_EQUAL(C4_PENDING, st);

  data_request_t* req = first_pending(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_NOT_NULL(req->url);
  TEST_ASSERT_NOT_NULL(strstr(req->url, "eth/v1/lodestar/states/0x"));
  TEST_ASSERT_NOT_NULL(strstr(req->url, state_hex));
  TEST_ASSERT_NOT_NULL(strstr(req->url, "/historical_summaries"));
  TEST_ASSERT_NULL(strstr(req->url, "nimbus/"));
  TEST_ASSERT_EQUAL_UINT32(BEACON_CLIENT_LODESTAR, req->preferred_client_type);
  TEST_ASSERT_EQUAL_UINT32(120, req->ttl);
  c4_prover_free(ctx);
}

void test_historical_summaries_nimbus_uses_nimbus_url(void) {
  bytes32_t state_root = {0};
  memset(state_root, 0xaa, 32);
  char state_hex[65];
  hex32(state_root, state_hex);

  uint8_t   header_bytes[112];
  ssz_def_t header_def  = SSZ_CONTAINER("BeaconBlockHeader", BEACON_BLOCK_HEADER);
  eth_block_t block     = dummy_block_with_state_root(state_root, &header_def, header_bytes);

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_MAINNET, C4_PROVER_FLAG_NIMBUS);
  TEST_ASSERT_NOT_NULL(ctx);

  json_t      history = {0};
  c4_status_t st      = c4_test_get_historical_summaries(ctx, &block, &history);
  TEST_ASSERT_EQUAL(C4_PENDING, st);

  data_request_t* req = first_pending(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_NOT_NULL(req->url);
  TEST_ASSERT_NOT_NULL(strstr(req->url, "nimbus/v1/debug/beacon/states/0x"));
  TEST_ASSERT_NOT_NULL(strstr(req->url, state_hex));
  TEST_ASSERT_NOT_NULL(strstr(req->url, "/historical_summaries"));
  TEST_ASSERT_NULL(strstr(req->url, "lodestar"));
  TEST_ASSERT_EQUAL_UINT32(BEACON_CLIENT_NIMBUS, req->preferred_client_type);
  TEST_ASSERT_EQUAL_UINT32(120, req->ttl);
  c4_prover_free(ctx);
}

void test_gloas_by_number_nimbus_scans_after_execution_slot(void) {
  bytes32_t parent_cl = {0};
  bytes32_t el_hash   = {0};
  bytes32_t exec      = {0};
  bytes32_t data      = {0};
  bytes32_t sig       = {0};
  memset(parent_cl, 0xab, 32);
  memset(el_hash, 0x11, 32);
  memset(exec, 0xcd, 32);
  memset(data, 0xee, 32);
  memset(sig, 0xff, 32);

  prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"0x1\",false]", C4_CHAIN_PLATABERGET, C4_PROVER_FLAG_NIMBUS);
  TEST_ASSERT_NOT_NULL(ctx);

  eth_block_t beacon_block = {0};
  json_t      block_id     = {.start = "\"0x1\"", .len = 5, .type = JSON_TYPE_STRING};
  c4_status_t st           = C4_PENDING;
  int         loops        = 16;
  int         saw_slot     = 0;
  int         saw_data     = 0;
  int         saw_sig      = 0;
  int         saw_block    = 0;

  while (st == C4_PENDING && loops-- > 0 && !saw_block) {
    st = c4_beacon_get_block_for_eth(ctx, block_id, &beacon_block);
    if (st != C4_PENDING) break;

    for (data_request_t* req = ctx->state.requests; req; req = req->next) {
      if (!c4_state_is_pending(req)) continue;
      if (req->url && strstr(req->url, "parent_root=")) {
        c4_prover_free(ctx);
        TEST_FAIL_MESSAGE("Nimbus Gloas path must not query headers?parent_root=");
      }
      if (is_eth_get_block_number(req, "0x1")) {
        fulfill(req, gloas_el_rpc(el_hash, parent_cl, 100));
        continue;
      }
      if (req->url && strstr(req->url, "eth/v1/beacon/headers?slot=100")) {
        saw_slot = 1;
        fulfill(req, header_list_json(exec, 100, parent_cl));
        continue;
      }
      if (req->url && strstr(req->url, "eth/v1/beacon/headers?slot=101")) {
        saw_data = 1;
        fulfill(req, header_list_json(data, 101, exec));
        continue;
      }
      if (req->url && strstr(req->url, "eth/v1/beacon/headers?slot=102")) {
        saw_sig = 1;
        fulfill(req, header_list_json(sig, 102, data));
        continue;
      }
      if (req->url && strstr(req->url, "eth/v1/beacon/headers?slot=")) {
        fulfill_const(req, k_empty_headers);
        continue;
      }
      if (req->url && strstr(req->url, "eth/v2/beacon/blocks/")) {
        saw_block = 1;
        continue;
      }
      c4_prover_free(ctx);
      TEST_FAIL_MESSAGE(req->url ? req->url : (char*) req->payload.data);
    }
  }

  TEST_ASSERT_TRUE(saw_slot);
  TEST_ASSERT_TRUE(saw_data);
  TEST_ASSERT_TRUE(saw_sig);
  TEST_ASSERT_TRUE_MESSAGE(saw_block, "Nimbus Gloas slot scan must reach a block fetch");
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
  RUN_TEST(test_parent_lookup_default_uses_parent_root_query);
  RUN_TEST(test_parent_lookup_default_empty_means_not_signed);
  RUN_TEST(test_parent_lookup_default_match_fetches_child_block);
  RUN_TEST(test_gloas_by_number_uses_slot_then_parent_root);
  RUN_TEST(test_gloas_by_number_nimbus_scans_after_execution_slot);
  RUN_TEST(test_gloas_by_hash_uses_slot_then_parent_root);
  RUN_TEST(test_gloas_without_slot_number_falls_back_to_n_plus_one);
  RUN_TEST(test_gloas_execution_slot_wrong_parent_errors);
  RUN_TEST(test_historical_summaries_default_uses_lodestar_url);
  RUN_TEST(test_historical_summaries_nimbus_uses_nimbus_url);
  return UNITY_END();
}
