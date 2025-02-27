#include "platform_compat.h"
#include "unity.h"
#include "util/bytes.h"
#include "util/json.h"
#include "util/patricia.h"
#include "util/plugin.h"
#include "util/ssz.h"
#include "verifier/verify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {
  setenv("C4_STATES_DIR", TESTDATA_DIR "/eth_getLogs1", 1);
}

void test_verify_only() {
  storage_plugin_t storage;
  verify_ctx_t     verify_ctx = {0};
  buffer_t         proof_buf  = {0};
  c4_get_storage_config(&storage);
  storage.get("proof.ssz", &proof_buf);

  c4_verify_from_bytes(&verify_ctx, proof_buf.data, "eth_getLogs", json_parse("[{\"address\":[\"0xdac17f958d2ee523a2206206994597c13d831ec7\"],\"fromBlock\":\"0x14d7970\",\"toBlock\":\"0x14d7970\"}]"), C4_CHAIN_MAINNET);
  buffer_free(&proof_buf);
}
int main(void) {
  setUp();
  test_verify_only();
}