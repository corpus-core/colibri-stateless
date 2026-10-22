#include "blst.h"
#include "c4_assert.h"
#include "eth_verify.h"

#ifdef USE_CHECKPOINTZ

enum { INITIAL_PERIOD = 1700, TARGET_PERIOD = 1716, COMMITTEE_SIZE = 512 };
static blst_scalar signing_keys[2][COMMITTEE_SIZE];
static uint8_t pubkeys[2][COMMITTEE_SIZE * 48];
static uint64_t header_nonce;
static const ssz_def_t header_def = SSZ_CONTAINER("BeaconBlockHeader", BEACON_BLOCK_HEADER);
static const ssz_def_t bits_def = SSZ_BIT_VECTOR("bits", COMMITTEE_SIZE);
static const ssz_def_t signature_def = SSZ_BYTE_VECTOR("signature", 96);

void setUp(void) {
  reset_local_filecache();
  bytes32_t previous = {0};
  TEST_ASSERT_TRUE(c4_set_sync_period(INITIAL_PERIOD, bytes(pubkeys[0], sizeof(pubkeys[0])),
                                     C4_CHAIN_MAINNET, previous));
}

void tearDown(void) {
  reset_local_filecache();
}

static void make_keys(unsigned family) {
  for (unsigned i = 0; i < COMMITTEE_SIZE; ++i) {
    uint8_t seed[32] = {0};
    uint32_to_le(seed, family + 1);
    uint32_to_le(seed + 4, i + 1);
    blst_keygen(&signing_keys[family][i], seed, sizeof(seed), NULL, 0);
    blst_p1 pk;
    blst_sk_to_pk_in_g1(&pk, &signing_keys[family][i]);
    blst_p1_compress(pubkeys[family] + 48 * i, &pk);
  }
}

static ssz_ob_t zero_ssz(const ssz_def_t* def) {
  if (def->type == SSZ_TYPE_CONTAINER) {
    ssz_builder_t builder = ssz_builder_for_def(def);
    for (unsigned i = 0; i < def->def.container.len; ++i) {
      const ssz_def_t* field = def->def.container.elements + i;
      ssz_ob_t value = zero_ssz(field);
      ssz_add_bytes(&builder, field->name, value.bytes);
      safe_free(value.bytes.data);
    }
    return ssz_builder_to_bytes(&builder);
  }
  if (ssz_is_list_type(def) || def->type == SSZ_TYPE_NONE)
    return (ssz_ob_t) {.def = def, .bytes = NULL_BYTES};
  uint32_t length = ssz_fixed_length(def);
  return (ssz_ob_t) {.def = def, .bytes = bytes(safe_calloc(length, 1), length)};
}

static void sign_header(ssz_ob_t header, uint64_t signature_slot, unsigned family,
                        uint8_t bits[64], uint8_t signature[96]) {
  uint8_t message[64];
  bytes32_t signing_root;
  ssz_hash_tree_root(header, message);
  TEST_ASSERT_TRUE(eth_calculate_domain(C4_CHAIN_MAINNET, signature_slot, message + 32));
  sha256(bytes(message, sizeof(message)), signing_root);
  static const char dst[] = "BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_POP_";
  blst_p2 point, aggregate;
  blst_hash_to_g2(&point, signing_root, 32, (const uint8_t*) dst, sizeof(dst) - 1, NULL, 0);
  memset(bits, 0, 64);
  for (unsigned i = 0; i < 342; ++i) {
    blst_p2 partial;
    blst_sign_pk_in_g1(&partial, &point, &signing_keys[family][i]);
    if (i == 0) aggregate = partial;
    else blst_p2_add_or_double(&aggregate, &aggregate, &partial);
    bits[i / 8] |= (uint8_t) (1u << (i % 8));
  }
  blst_p2_compress(signature, &aggregate);
}

static void merkle_branch(uint8_t tree[][32], uint64_t index, uint8_t* out) {
  while (index > 1) {
    memcpy(out, tree[index ^ 1], 32);
    out += 32;
    index >>= 1;
  }
}

// Each update has real aggregate signatures and coherent finality/committee branches.
static ssz_ob_t signed_update(uint32_t period, unsigned signing_family) {
  uint64_t slot = (uint64_t) period * 8192 + 100;
  const chain_spec_t* spec = c4_eth_get_chain_spec(C4_CHAIN_MAINNET);
  fork_id_t fork = c4_chain_fork_id(C4_CHAIN_MAINNET, epoch_for_slot(slot, spec));
  ssz_ob_t update = zero_ssz(eth_get_light_client_update(fork));
  ssz_ob_t attested = ssz_get(&update, "attestedHeader");
  ssz_ob_t beacon = ssz_get(&attested, "beacon");
  ssz_ob_t finalized = ssz_get(&update, "finalizedHeader");
  ssz_ob_t finalized_beacon = ssz_get(&finalized, "beacon");
  uint64_to_le(beacon.bytes.data, slot);
  uint64_to_le(beacon.bytes.data + 8, ++header_nonce);
  uint64_to_le(finalized_beacon.bytes.data, slot - 64);
  memset(finalized_beacon.bytes.data + 16, (int) (period % 32), 96);
  ssz_ob_t next = ssz_get(&update, "nextSyncCommittee");
  memcpy(ssz_get(&next, "pubkeys").bytes.data, pubkeys[1], sizeof(pubkeys[1]));

  uint8_t tree[256][32] = {{0}};
  uint64_t next_index = c4_next_sync_committee_gindex(C4_CHAIN_MAINNET, slot);
  uint64_t final_index = c4_finalized_root_gindex(C4_CHAIN_MAINNET, slot);
  TEST_ASSERT_LESS_THAN_UINT64(256, final_index);
  ssz_hash_tree_root(next, tree[next_index]);
  ssz_hash_tree_root(finalized_beacon, tree[final_index]);
  unsigned bottom = 1;
  while (bottom <= final_index / 2) bottom <<= 1;
  for (unsigned i = bottom - 1; i > 0; --i) {
    if (i != next_index)
      sha256_merkle(bytes(tree[2 * i], 32), bytes(tree[2 * i + 1], 32), tree[i]);
  }
  memcpy(ssz_get(&beacon, "stateRoot").bytes.data, tree[1], 32);
  merkle_branch(tree, next_index, ssz_get(&update, "nextSyncCommitteeBranch").bytes.data);
  merkle_branch(tree, final_index, ssz_get(&update, "finalityBranch").bytes.data);
  ssz_ob_t aggregate = ssz_get(&update, "syncAggregate");
  sign_header(beacon, slot, signing_family,
               ssz_get(&aggregate, "syncCommitteeBits").bytes.data,
               ssz_get(&aggregate, "syncCommitteeSignature").bytes.data);
  uint64_to_le(ssz_get(&update, "signatureSlot").bytes.data, slot + 1);
  return update;
}

static bytes_t update_chain(unsigned count) {
  buffer_t encoded = {0};
  const chain_spec_t* spec = c4_eth_get_chain_spec(C4_CHAIN_MAINNET);
  for (unsigned i = 0; i < count; ++i) {
    ssz_ob_t update = signed_update(INITIAL_PERIOD + i, i == 0 ? 0 : 1);
    uint8_t prefix[12];
    uint64_to_le(prefix, update.bytes.len + 4);
    fork_id_t fork = c4_chain_fork_id(C4_CHAIN_MAINNET, (uint64_t) (INITIAL_PERIOD + i) << spec->epochs_per_period_bits);
    TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_MAINNET, fork, prefix + 8));
    buffer_append(&encoded, bytes(prefix, sizeof(prefix)));
    buffer_append(&encoded, update.bytes);
    safe_free(update.bytes.data);
  }
  return encoded.data;
}

static ssz_ob_t bootstrap_for(uint32_t period, unsigned family, bytes32_t checkpoint_root) {
  uint64_t slot = (uint64_t) period * 8192;
  const chain_spec_t* spec = c4_eth_get_chain_spec(C4_CHAIN_MAINNET);
  fork_id_t fork = c4_chain_fork_id(C4_CHAIN_MAINNET, epoch_for_slot(slot, spec));
  ssz_ob_t bootstrap = zero_ssz(eth_get_light_client_bootstrap(fork));
  ssz_ob_t header = ssz_get(&bootstrap, "header");
  ssz_ob_t beacon = ssz_get(&header, "beacon");
  uint64_to_le(beacon.bytes.data, slot);
  ssz_ob_t committee = ssz_get(&bootstrap, "currentSyncCommittee");
  memcpy(ssz_get(&committee, "pubkeys").bytes.data, pubkeys[family], sizeof(pubkeys[family]));
  bytes32_t committee_root, state_root;
  ssz_hash_tree_root(committee, committee_root);
  ssz_verify_single_merkle_proof(ssz_get(&bootstrap, "currentSyncCommitteeBranch").bytes,
                                 committee_root, c4_current_sync_committee_gindex(C4_CHAIN_MAINNET, slot), state_root);
  memcpy(ssz_get(&beacon, "stateRoot").bytes.data, state_root, 32);
  ssz_hash_tree_root(beacon, checkpoint_root);
  return bootstrap;
}

static c4_status_t get_validators(verify_ctx_t* ctx, uint32_t period) {
  c4_sync_validators_t validators = {0};
  c4_status_t status = c4_get_validators(ctx, period, &validators, NULL);
  safe_free(validators.validators.data);
  return status;
}

static void begin_sync(verify_ctx_t* ctx, uint32_t target) {
  ctx->chain_id = C4_CHAIN_MAINNET;
  TEST_ASSERT_EQUAL_INT(C4_PENDING, get_validators(ctx, target));
  data_request_t* req = c4_state_get_pending_request(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);
  req->response = update_chain(target - INITIAL_PERIOD);
}

static void assert_no_provisional_periods(void) {
  c4_chain_state_t state = c4_get_chain_state(C4_CHAIN_MAINNET);
  TEST_ASSERT_EQUAL_INT(C4_STATE_SYNC_PERIODS, state.status);
  TEST_ASSERT_EQUAL_UINT32(INITIAL_PERIOD, state.data.periods[0]);
  for (unsigned i = 1; i < MAX_SYNC_PERIODS; ++i)
    TEST_ASSERT_EQUAL_UINT32(0, state.data.periods[i]);
  buffer_t stored = {0};
  bool found = file_get("sync_1_1716", &stored);
  buffer_free(&stored);
  TEST_ASSERT_FALSE(found);
}

static void answer_checkpoint(verify_ctx_t* ctx, bytes32_t checkpoint_root) {
  data_request_t* req = c4_state_get_pending_request(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_EQUAL_INT(C4_DATA_TYPE_CHECKPOINTZ, req->type);
  char* json = bprintf(NULL, "{\"data\":{\"finalized\":{\"epoch\":\"%l\",\"root\":\"0x%x\"}}}",
                        (uint64_t) TARGET_PERIOD * 256, bytes(checkpoint_root, 32));
  req->response = bytes((uint8_t*) json, strlen(json));
  TEST_ASSERT_EQUAL_INT(C4_PENDING, get_validators(ctx, TARGET_PERIOD));
}

static void answer_bootstrap(verify_ctx_t* ctx, ssz_ob_t bootstrap) {
  data_request_t* req = c4_state_get_pending_request(&ctx->state);
  TEST_ASSERT_NOT_NULL(req);
  TEST_ASSERT_EQUAL_INT(C4_DATA_TYPE_BEACON_API, req->type);
  req->response = bootstrap.bytes; // The request owns the allocation from here on.
}

static c4_status_t verify_candidate_header(void) {
  uint8_t header_bytes[112] = {0}, bits[64], signature[96];
  uint64_t slot = (uint64_t) TARGET_PERIOD * 8192 + 123;
  uint64_to_le(header_bytes, slot);
  uint64_to_le(header_bytes + 8, ++header_nonce);
  ssz_ob_t header = {.def = &header_def, .bytes = bytes(header_bytes, sizeof(header_bytes))};
  sign_header(header, slot + 1, 1, bits, signature);
  ssz_ob_t bits_ob = {.def = &bits_def, .bytes = bytes(bits, sizeof(bits))};
  ssz_ob_t sig_ob = {.def = &signature_def, .bytes = bytes(signature, sizeof(signature))};
  verify_ctx_t ctx = {.chain_id = C4_CHAIN_MAINNET};
  c4_status_t status = c4_verify_blockroot_signature(&ctx, &header, &bits_ob, &sig_ob, 0, NULL);
  c4_state_free(&ctx.state);
  return status;
}

void test_pending_checkpoint_keeps_updates_private(void) {
  verify_ctx_t ctx = {0};
  begin_sync(&ctx, TARGET_PERIOD);
  TEST_ASSERT_EQUAL_INT(C4_PENDING, get_validators(&ctx, TARGET_PERIOD));
  assert_no_provisional_periods();
  TEST_ASSERT_EQUAL_INT(C4_PENDING, verify_candidate_header());
  TEST_ASSERT_EQUAL_INT(C4_PENDING, get_validators(&ctx, TARGET_PERIOD));
  assert_no_provisional_periods();
  c4_state_free(&ctx.state); // Abandoning validation must not make its committees trusted.
  assert_no_provisional_periods();
  TEST_ASSERT_EQUAL_INT(C4_PENDING, verify_candidate_header());
}

void test_checkpoint_error_does_not_publish_updates(void) {
  verify_ctx_t ctx = {0};
  begin_sync(&ctx, TARGET_PERIOD);
  TEST_ASSERT_EQUAL_INT(C4_PENDING, get_validators(&ctx, TARGET_PERIOD));
  data_request_t* req = c4_state_get_pending_request(&ctx.state);
  TEST_ASSERT_NOT_NULL(req);
  req->error = strdup("checkpoint unavailable");
  TEST_ASSERT_EQUAL_INT(C4_ERROR, get_validators(&ctx, TARGET_PERIOD));
  assert_no_provisional_periods();
  c4_state_free(&ctx.state);
}

void test_valid_checkpoint_publishes_updates_only_after_bootstrap(void) {
  verify_ctx_t ctx = {0};
  begin_sync(&ctx, TARGET_PERIOD);
  TEST_ASSERT_EQUAL_INT(C4_PENDING, get_validators(&ctx, TARGET_PERIOD));
  bytes32_t root;
  ssz_ob_t bootstrap = bootstrap_for(TARGET_PERIOD, 1, root);
  answer_checkpoint(&ctx, root);
  assert_no_provisional_periods();
  TEST_ASSERT_EQUAL_INT(C4_PENDING, verify_candidate_header());
  answer_bootstrap(&ctx, bootstrap);
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, get_validators(&ctx, TARGET_PERIOD));
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, verify_candidate_header());
  c4_state_free(&ctx.state);
}

static void reject_tampered_bootstrap(bool replace_committee) {
  verify_ctx_t ctx = {0};
  begin_sync(&ctx, TARGET_PERIOD);
  bytes32_t root;
  ssz_ob_t bootstrap = bootstrap_for(TARGET_PERIOD, replace_committee ? 0 : 1, root);
  if (replace_committee) {
    ssz_ob_t committee = ssz_get(&bootstrap, "currentSyncCommittee");
    // Make the claimed keys agree with the LCU chain, but not with the anchored stateRoot.
    memcpy(ssz_get(&committee, "pubkeys").bytes.data, pubkeys[1], sizeof(pubkeys[1]));
  }
  else {
    ssz_ob_t branch = ssz_get(&bootstrap, "currentSyncCommitteeBranch");
    branch.bytes.data[0] ^= 1;
  }
  // Pre-deliver both anchor responses to exercise the Merkle check independently
  // of the pending-request cache isolation regression above.
  data_request_t* checkpoint = safe_calloc(1, sizeof(*checkpoint));
  checkpoint->chain_id = C4_CHAIN_MAINNET;
  checkpoint->type = C4_DATA_TYPE_CHECKPOINTZ;
  checkpoint->encoding = C4_DATA_ENCODING_JSON;
  checkpoint->url = strdup("eth/v1/beacon/states/head/finality_checkpoints");
  char* json = bprintf(NULL, "{\"data\":{\"finalized\":{\"epoch\":\"%l\",\"root\":\"0x%x\"}}}",
                        (uint64_t) TARGET_PERIOD * 256, bytes(root, 32));
  checkpoint->response = bytes((uint8_t*) json, strlen(json));
  c4_state_add_request(&ctx.state, checkpoint);
  data_request_t* response = safe_calloc(1, sizeof(*response));
  response->chain_id = C4_CHAIN_MAINNET;
  response->type = C4_DATA_TYPE_BEACON_API;
  response->encoding = C4_DATA_ENCODING_SSZ;
  response->url = bprintf(NULL, "eth/v1/beacon/light_client/bootstrap/0x%x", bytes(root, 32));
  response->response = bootstrap.bytes;
  c4_state_add_request(&ctx.state, response);
  TEST_ASSERT_EQUAL_INT(C4_ERROR, get_validators(&ctx, TARGET_PERIOD));
  TEST_ASSERT_NOT_EQUAL(C4_SUCCESS, verify_candidate_header());
  c4_state_free(&ctx.state);
}

void test_checkpoint_rejects_substituted_bootstrap_committee(void) {
  reject_tampered_bootstrap(true);
}

void test_checkpoint_rejects_tampered_bootstrap_branch(void) {
  reject_tampered_bootstrap(false);
}

void test_updates_within_subjectivity_window_do_not_require_checkpoint(void) {
  verify_ctx_t ctx = {0};
  begin_sync(&ctx, INITIAL_PERIOD + 1);
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, get_validators(&ctx, INITIAL_PERIOD + 1));
  TEST_ASSERT_NULL(c4_state_get_pending_request(&ctx.state));
  c4_state_free(&ctx.state);
}

void test_explicit_checkpoint_optout_still_allows_sync(void) {
  verify_ctx_t ctx = {.flags = VERIFY_FLAG_SKIP_WSP_CHECK};
  begin_sync(&ctx, TARGET_PERIOD);
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, get_validators(&ctx, TARGET_PERIOD));
  TEST_ASSERT_NULL(c4_state_get_pending_request(&ctx.state));
  TEST_ASSERT_EQUAL_INT(C4_SUCCESS, verify_candidate_header());
  c4_state_free(&ctx.state);
}

int main(void) {
  make_keys(0);
  make_keys(1);
  UNITY_BEGIN();
  RUN_TEST(test_pending_checkpoint_keeps_updates_private);
  RUN_TEST(test_checkpoint_error_does_not_publish_updates);
  RUN_TEST(test_valid_checkpoint_publishes_updates_only_after_bootstrap);
  RUN_TEST(test_checkpoint_rejects_substituted_bootstrap_committee);
  RUN_TEST(test_checkpoint_rejects_tampered_bootstrap_branch);
  RUN_TEST(test_updates_within_subjectivity_window_do_not_require_checkpoint);
  RUN_TEST(test_explicit_checkpoint_optout_still_allows_sync);
  return UNITY_END();
}

#else
void setUp(void) {}
void tearDown(void) {}
void test_checkpointz_disabled(void) { TEST_IGNORE_MESSAGE("USE_CHECKPOINTZ disabled"); }
int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_checkpointz_disabled);
  return UNITY_END();
}
#endif
