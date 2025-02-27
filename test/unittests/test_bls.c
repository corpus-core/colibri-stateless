#include "c4_assert.h"
#include "proofer/eth_req.h"
#include "unity.h"
#include "util/bytes.h"
#include "util/json.h"
#include "util/patricia.h"
#include "util/ssz.h"
void setUp(void) {
  // Initialisierung vor jedem Test (falls erforderlich)
}

void tearDown(void) {
  // Bereinigung nach jedem Test (falls erforderlich)
}

void test_bls() {
}
int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_bls);
  //  RUN_TEST(test_basic);
  return UNITY_END();
}