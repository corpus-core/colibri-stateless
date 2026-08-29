/*
 * Copyright 2026 corpus.core
 * SPDX-License-Identifier: MIT
 *
 * Unit tests for `c4_state_cache_get` / `c4_state_cache_set`.
 * These lock the two invariants that the snapshot call-site tests never hit:
 * a non-CACHE request with the same id must not be read as a snapshot, and
 * `cache_set` must not overwrite that request's response.
 */

#include "unity.h"
#include "util/bytes.h"
#include "util/state.h"
#include <string.h>

void setUp(void) {
}

void tearDown(void) {
}

void test_cache_get_ignores_non_cache_id(void) {
  c4_state_t state = {0};
  uint8_t    key[32];
  memset(key, 0x11, sizeof(key));

  data_request_t* io = safe_calloc(1, sizeof(data_request_t));
  io->type           = C4_DATA_TYPE_ETH_RPC;
  io->response       = bytes_dup(bytes((uint8_t*) "rpc", 3));
  memcpy(io->id, key, 32);
  c4_state_add_request(&state, io);

  bytes_t got = c4_state_cache_get(&state, key);
  TEST_ASSERT_NULL(got.data);
  TEST_ASSERT_EQUAL_INT(C4_DATA_TYPE_ETH_RPC, io->type);
  TEST_ASSERT_EQUAL_MEMORY("rpc", io->response.data, 3);

  c4_state_free(&state);
}

void test_cache_set_does_not_clobber_io(void) {
  c4_state_t state = {0};
  uint8_t    key[32];
  memset(key, 0x22, sizeof(key));

  data_request_t* io = safe_calloc(1, sizeof(data_request_t));
  io->type           = C4_DATA_TYPE_BEACON_API;
  io->response       = bytes_dup(bytes((uint8_t*) "beacon", 6));
  memcpy(io->id, key, 32);
  c4_state_add_request(&state, io);
  uint8_t* io_resp = io->response.data;

  c4_state_cache_set(&state, key, bytes_dup(bytes((uint8_t*) "snap", 4)));

  TEST_ASSERT_EQUAL_INT(C4_DATA_TYPE_BEACON_API, io->type);
  TEST_ASSERT_EQUAL_PTR(io_resp, io->response.data);

  bytes_t got = c4_state_cache_get(&state, key);
  TEST_ASSERT_EQUAL_MEMORY("snap", got.data, 4);

  uint32_t same_id = 0;
  for (data_request_t* r = state.requests; r; r = r->next)
    if (memcmp(r->id, key, 32) == 0) same_id++;
  TEST_ASSERT_EQUAL_UINT32(2, same_id);

  c4_state_free(&state);
}

void test_cache_get_skips_io_in_front(void) {
  c4_state_t state = {0};
  uint8_t    key[32];
  memset(key, 0x33, sizeof(key));

  c4_state_cache_set(&state, key, bytes_dup(bytes((uint8_t*) "snap", 4)));

  data_request_t* io = safe_calloc(1, sizeof(data_request_t));
  io->type           = C4_DATA_TYPE_ETH_RPC;
  io->response       = bytes_dup(bytes((uint8_t*) "rpc", 3));
  memcpy(io->id, key, 32);
  c4_state_add_request(&state, io);

  bytes_t got = c4_state_cache_get(&state, key);
  TEST_ASSERT_EQUAL_MEMORY("snap", got.data, 4);

  c4_state_free(&state);
}

void test_cache_set_appends_so_get_by_id_keeps_io(void) {
  c4_state_t state = {0};
  uint8_t    key[32];
  memset(key, 0x44, sizeof(key));

  data_request_t* io = safe_calloc(1, sizeof(data_request_t));
  io->type           = C4_DATA_TYPE_ETH_RPC;
  io->response       = bytes_dup(bytes((uint8_t*) "rpc", 3));
  memcpy(io->id, key, 32);
  c4_state_add_request(&state, io);

  c4_state_cache_set(&state, key, bytes_dup(bytes((uint8_t*) "snap", 4)));

  data_request_t* found = c4_state_get_data_request_by_id(&state, key);
  TEST_ASSERT_EQUAL_PTR(io, found);
  TEST_ASSERT_EQUAL_INT(C4_DATA_TYPE_ETH_RPC, found->type);
  TEST_ASSERT_EQUAL_MEMORY("rpc", found->response.data, 3);

  bytes_t got = c4_state_cache_get(&state, key);
  TEST_ASSERT_EQUAL_MEMORY("snap", got.data, 4);

  c4_state_free(&state);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_cache_get_ignores_non_cache_id);
  RUN_TEST(test_cache_set_does_not_clobber_io);
  RUN_TEST(test_cache_get_skips_io_in_front);
  RUN_TEST(test_cache_set_appends_so_get_by_id_keeps_io);
  return UNITY_END();
}
