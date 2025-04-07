// datei: test_addiere.c
#include "beacon_types.h"
#include "bytes.h"
#include "c4_assert.h"
#include "ssz.h"
#include "unity.h"
void setUp(void) {
}

void tearDown(void) {
}

void test_chainId() {
  verify_ctx_t ctx    = {0};
  c4_status_t  status = c4_verify_from_bytes(&ctx, NULL_BYTES, "eth_chainId", (json_t) {.start = "[]", .len = 2, .type = JSON_TYPE_ARRAY}, C4_CHAIN_MAINNET);
  TEST_ASSERT_MESSAGE(status == C4_SUCCESS, "c4_verify_from_bytes failed");
  TEST_ASSERT_MESSAGE(ctx.data.bytes.len == 8 && ctx.data.bytes.data[0] == 0x01, "c4_verify_from_bytes failed");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_chainId);
  return UNITY_END();
}