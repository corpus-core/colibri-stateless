// datei: test_addiere.c
#include "c4_assert.h"
#include "unity.h"
#include "util/bytes.h"
#include "util/ssz.h"
void setUp(void) {
}

void tearDown(void) {
}

void test_ssz() {
  buffer_t buf  = {0};
  buffer_t tmp  = {0};
  bytes_t  data = read_testdata("body.ssz");
  ssz_ob_t ssz  = ssz_ob(SIGNED_BEACON_BLOCK_CONTAINER, data);

  json_t json      = json_parse(bprintf(&buf, "%z\n", ssz));
  json_t signature = json_get(json, "signature");
  char*  sig       = json_as_string(signature, &tmp);
  TEST_ASSERT_EQUAL_STRING("0xb54bfc2475721ef6377a50017bb94064272a8d9190a055d032c5c4fe28d26c7c4fc5864778df1eebe9b943372e2e52ae068776ce8aec4c1bcf4d9dda5a72fd86e3d13e7b3b5dfe8ce9a59ec91e62f576d9d7ea8bba10c90bd6d5ff6c506fbecc", sig);
  buffer_free(&tmp);
  buffer_free(&buf);
  free(data.data);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ssz);
  return UNITY_END();
}