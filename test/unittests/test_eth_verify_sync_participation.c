// Regression test for sync-committee participation threshold on blockroot
// signature verification.
//
// The light-client acceptance rule requires at least ceil(2/3 * 512) = 342
// of the 512 sync-committee members to have contributed to an aggregate
// signature before it can authenticate a block root. The native C verifier
// previously accepted *any* subset with a valid aggregate — the SP1 guest
// already enforced the 342-member threshold, so the two verification routes
// enforced different rules. This test sweeps the 1..342 boundary for the
// current-period committee, checks the previous-committee mid-period edge,
// and keeps a corrupted-signature control on the accepting side.

#include "beacon_types.h"
#include "eth_verify.h"
#include "blst.h"
#include "c4_assert.h"
#include "sync_committee.h"
#include "unity.h"

typedef struct { char* key; bytes_t value; } kojem_t;
static kojem_t       kojem_items[64];
static blst_scalar   threshold_keys_a[512], threshold_keys_b[512];
static uint8_t       threshold_pubs_a[512 * 48], threshold_pubs_b[512 * 48];
static uint64_t      threshold_nonce = 0;
static const ssz_def_t header_def     = SSZ_CONTAINER("BeaconBlockHeader", BEACON_BLOCK_HEADER);
static const ssz_def_t bits_def       = SSZ_BIT_VECTOR("bits", 512);
static const ssz_def_t signature_def_v = SSZ_BYTE_VECTOR("sig", 96);

static bool memory_get(char* key, buffer_t* buf) {
  for (size_t i = 0; i < 64; ++i)
    if (kojem_items[i].key && !strcmp(key, kojem_items[i].key)) {
      buffer_append(buf, kojem_items[i].value);
      return true;
    }
  return false;
}
static void memory_del(char* key) {
  for (size_t i = 0; i < 64; ++i)
    if (kojem_items[i].key && !strcmp(key, kojem_items[i].key)) {
      free(kojem_items[i].key);
      free(kojem_items[i].value.data);
      kojem_items[i] = (kojem_t) {0};
      return;
    }
}
static void memory_set(char* key, bytes_t value) {
  memory_del(key);
  for (size_t i = 0; i < 64; ++i)
    if (!kojem_items[i].key) {
      kojem_items[i].key   = strdup(key);
      kojem_items[i].value = bytes_dup(value);
      return;
    }
  abort();
}
static void memory_clear(void) {
  for (size_t i = 0; i < 64; ++i)
    if (kojem_items[i].key) {
      free(kojem_items[i].key);
      free(kojem_items[i].value.data);
      kojem_items[i] = (kojem_t) {0};
    }
}

void setUp(void)    { }
void tearDown(void) { }

static void make_threshold_keys(unsigned family, blst_scalar* keys, uint8_t* pubkeys) {
  for (unsigned i = 0; i < 512; ++i) {
    uint8_t ikm[32] = {0};
    uint32_to_le(ikm, family);
    uint32_to_le(ikm + 4, i + 1);
    blst_keygen(keys + i, ikm, sizeof(ikm), NULL, 0);
    blst_p1 pk;
    blst_sk_to_pk_in_g1(&pk, keys + i);
    blst_p1_compress(pubkeys + 48 * i, &pk);
  }
}

static void sign_threshold_header(ssz_ob_t header, uint64_t signature_slot, blst_scalar* keys,
                                  unsigned count, uint8_t* bits, uint8_t* sig_bytes) {
  bytes32_t header_root, signing_root;
  uint8_t   message[64];
  ssz_hash_tree_root(header, header_root);
  memcpy(message, header_root, 32);
  if (!eth_calculate_domain(C4_CHAIN_MAINNET, signature_slot, message + 32)) fprintf(stderr, "domain calculation failed\n");
  sha256(bytes(message, 64), signing_root);
  const char* dst = "BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_POP_";
  blst_p2   point, aggregated;
  blst_hash_to_g2(&point, signing_root, 32, (const uint8_t*) dst, strlen(dst), NULL, 0);
  memset(bits, 0, 64);
  for (unsigned i = 0; i < count; ++i) {
    blst_p2 sig;
    blst_sign_pk_in_g1(&sig, &point, keys + i);
    if (i == 0)
      aggregated = sig;
    else
      blst_p2_add_or_double(&aggregated, &aggregated, &sig);
    bits[i / 8] |= (uint8_t) (1 << (i % 8));
  }
  blst_p2_compress(sig_bytes, &aggregated);
}

static c4_status_t run_threshold_case(uint64_t header_slot, blst_scalar* keys, unsigned participants,
                                      bool corrupt, const char** out_error) {
  uint8_t header_bytes[112] = {0}, bits[64] = {0}, sig_bytes[96] = {0};
  uint64_to_le(header_bytes, header_slot);
  uint64_to_le(header_bytes + 8, ++threshold_nonce);
  memset(header_bytes + 16, (uint8_t) threshold_nonce, 96);
  ssz_ob_t header = {.def = &header_def, .bytes = bytes(header_bytes, 112)};
  sign_threshold_header(header, header_slot + 1, keys, participants, bits, sig_bytes);
  if (corrupt) sig_bytes[20] ^= 1;
  ssz_ob_t bits_ob = {.def = &bits_def, .bytes = bytes(bits, 64)};
  ssz_ob_t sig_ob  = {.def = &signature_def_v, .bytes = bytes(sig_bytes, 96)};
  verify_ctx_t ctx = {0};
  ctx.chain_id     = C4_CHAIN_MAINNET;
  c4_status_t result = c4_verify_blockroot_signature(&ctx, &header, &bits_ob, &sig_ob, 0, NULL);
  if (out_error) *out_error = ctx.state.error ? strdup(ctx.state.error) : NULL;
  c4_state_free(&ctx.state);
  return result;
}

static void setup_sync_periods(void) {
  memory_clear();
  storage_plugin_t storage = {
      .get             = memory_get,
      .set             = memory_set,
      .del             = memory_del,
      .max_sync_states = 8};
  c4_set_storage_config(&storage);
  bytes32_t previous = {0};
  TEST_ASSERT_TRUE(c4_set_sync_period(1500, bytes(threshold_pubs_a, sizeof(threshold_pubs_a)),
                                      C4_CHAIN_MAINNET, previous));
  TEST_ASSERT_TRUE(c4_set_sync_period(1501, bytes(threshold_pubs_b, sizeof(threshold_pubs_b)),
                                      C4_CHAIN_MAINNET, previous));
}

void test_one_signer_is_rejected(void) {
  make_threshold_keys(1, threshold_keys_a, threshold_pubs_a);
  make_threshold_keys(2, threshold_keys_b, threshold_pubs_b);
  setup_sync_periods();
  const char* error = NULL;
  c4_status_t result = run_threshold_case(1501ull * 8192 + 100, threshold_keys_b, 1, false, &error);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, result);
  TEST_ASSERT_NOT_NULL(error);
  TEST_ASSERT_NOT_NULL(strstr(error, "insufficient sync committee participation"));
  memory_clear();
}

void test_below_two_thirds_is_rejected(void) {
  make_threshold_keys(1, threshold_keys_a, threshold_pubs_a);
  make_threshold_keys(2, threshold_keys_b, threshold_pubs_b);
  setup_sync_periods();
  const char* error = NULL;
  c4_status_t result = run_threshold_case(1501ull * 8192 + 101, threshold_keys_b, 341, false, &error);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, result);
  TEST_ASSERT_NOT_NULL(error);
  TEST_ASSERT_NOT_NULL(strstr(error, "insufficient sync committee participation"));
  memory_clear();
}

void test_exact_two_thirds_is_accepted(void) {
  make_threshold_keys(1, threshold_keys_a, threshold_pubs_a);
  make_threshold_keys(2, threshold_keys_b, threshold_pubs_b);
  setup_sync_periods();
  const char* error = NULL;
  c4_status_t result = run_threshold_case(1501ull * 8192 + 102, threshold_keys_b, 342, false, &error);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, result, error ? error : "342-member aggregate must be accepted");
  memory_clear();
}

void test_previous_committee_at_mid_period_is_accepted(void) {
  make_threshold_keys(1, threshold_keys_a, threshold_pubs_a);
  make_threshold_keys(2, threshold_keys_b, threshold_pubs_b);
  setup_sync_periods();
  const char* error = NULL;
  c4_status_t result = run_threshold_case(1501ull * 8192 + 4000, threshold_keys_a, 342, false, &error);
  TEST_ASSERT_EQUAL_INT_MESSAGE(C4_SUCCESS, result, error ? error : "previous committee at mid period must pass");
  memory_clear();
}

void test_corrupted_signature_remains_rejected(void) {
  make_threshold_keys(1, threshold_keys_a, threshold_pubs_a);
  make_threshold_keys(2, threshold_keys_b, threshold_pubs_b);
  setup_sync_periods();
  const char* error = NULL;
  c4_status_t result = run_threshold_case(1501ull * 8192 + 104, threshold_keys_b, 342, true, &error);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, result);
  TEST_ASSERT_NOT_NULL(error);
  TEST_ASSERT_NOT_NULL(strstr(error, "invalid blockhash signature"));
  memory_clear();
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_one_signer_is_rejected);
  RUN_TEST(test_below_two_thirds_is_rejected);
  RUN_TEST(test_exact_two_thirds_is_accepted);
  RUN_TEST(test_previous_committee_at_mid_period_is_accepted);
  RUN_TEST(test_corrupted_signature_remains_rejected);
  return UNITY_END();
}
