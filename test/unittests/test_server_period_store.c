/*
 * Server tests for period_store: set_block/write verification and LCU cache read
 */

#include "unity.h"

#ifdef HTTP_SERVER

#include "../../src/chains/eth/server/eth_conf.h"
#include "beacon_types.h"
#include "chains/eth/server/period_store.h"
#include "chain_spec.h"
#include "crypto.h"
#include "state.h"
#include "test_server_helper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#endif

#ifdef _WIN32
#include <windows.h>
static int win_mkdir(const char* path) {
  if (CreateDirectoryA(path, NULL)) return 0;
  DWORD err = GetLastError();
  return (err == ERROR_ALREADY_EXISTS) ? 0 : -1;
}
#define MKDIR(p) win_mkdir(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

static char g_ps_path[512];

static void ensure_dir(const char* path) {
  MKDIR(path);
}

void setUp(void) {
  http_server_t config = (http_server_t) {0};
  config.port          = TEST_PORT;
  config.host          = TEST_HOST;
  config.chain_id      = 1;
  // Dedicated period_store dir for this test binary
  snprintf(g_ps_path, sizeof(g_ps_path), "%s/server/period_store_tests", TESTDATA_DIR);
  ensure_dir(g_ps_path);
  eth_config.period_store = g_ps_path;
  // Disable backfill effects for this phase
  eth_config.period_backfill_max_periods = 0;
  c4_test_server_setup(&config);
}

void tearDown(void) {
  c4_test_server_teardown();
}

static void build_header112(uint8_t out[112], const uint8_t parent_root[32]) {
  memset(out, 0, 112);
  // slot (little endian uint64_t)
  uint64_to_le(out, 0);
  // proposer_index (uint64_t LE) left 0
  // parent_root at +16
  memcpy(out + 16, parent_root, 32);
  // state_root at +48 (zero)
  // body_root at +80 (zero)
}

void test_period_store_set_block_write(void) {
  // Choose a deterministic slot within a known period
  const uint64_t SLOTS_PER_PERIOD = 8192;
  uint64_t       slot             = SLOTS_PER_PERIOD * 2 + 123; // period 2, index 123
  uint64_t       period           = slot / SLOTS_PER_PERIOD;
  uint64_t       idx              = slot % SLOTS_PER_PERIOD;

  uint8_t root[32];
  uint8_t parent[32];
  for (int i = 0; i < 32; i++) {
    root[i]   = (uint8_t) 0xA5;
    parent[i] = (uint8_t) 0x5A;
  }
  uint8_t header112[112];
  build_header112(header112, parent);

  // Invoke writer
  c4_period_sync_on_head(slot, root, header112);

  // Wait for async write to complete by polling for file size change
  char dir[512];
  snprintf(dir, sizeof(dir), "%s/%lu", g_ps_path, (unsigned long) period);
  char blocks_path[512], headers_path[512];
  snprintf(blocks_path, sizeof(blocks_path), "%s/" C4_PS_BLOCKS_SSZ, dir);
  snprintf(headers_path, sizeof(headers_path), "%s/" C4_PS_HEADERS_SSZ, dir);

  int tries = 0;
  while (tries++ < 200) { // ~200ms
    uv_run(uv_default_loop(), UV_RUN_NOWAIT);
#ifndef _WIN32
    usleep(1000);
#else
    Sleep(1);
#endif
    FILE* fb = fopen(blocks_path, "rb");
    FILE* fh = fopen(headers_path, "rb");
    if (fb && fh) {
      fclose(fb);
      fclose(fh);
      break;
    }
    if (fb) fclose(fb);
    if (fh) fclose(fh);
  }

  // Verify content at expected offsets
  {
    FILE* f = fopen(blocks_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    int rc = fseek(f, (long) (idx * 32), SEEK_SET);
    TEST_ASSERT_EQUAL(0, rc);
    uint8_t buf[32] = {0};
    size_t  n       = fread(buf, 1, 32, f);
    fclose(f);
    TEST_ASSERT_EQUAL(32, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(root, buf, 32);
  }
  {
    FILE* f = fopen(headers_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    long off = (long) (idx * 112);
    int  rc  = fseek(f, off, SEEK_SET);
    TEST_ASSERT_EQUAL(0, rc);
    uint8_t buf[112] = {0};
    size_t  n        = fread(buf, 1, 112, f);
    fclose(f);
    TEST_ASSERT_EQUAL(112, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(header112, buf, 112);
  }
}

typedef struct {
  bytes_t out;
  char*   err;
  int     done;
} lcu_ctx_t;

static void lcu_cb(void* user_data, bytes_t updates, char* error) {
  lcu_ctx_t* c = (lcu_ctx_t*) user_data;
  c->out       = updates;
  c->err       = error;
  c->done      = 1;
}

static void pump_until(int* done) {
  for (int i = 0; i < 400 && !*done; i++) {
    uv_run(uv_default_loop(), UV_RUN_NOWAIT);
#ifndef _WIN32
    usleep(1000);
#else
    Sleep(1);
#endif
  }
}

static void add_zeros(ssz_builder_t* b, const char* name, uint32_t n) {
  uint8_t* z = (uint8_t*) safe_calloc(n ? n : 1, 1);
  ssz_add_bytes(b, name, bytes(z, n));
  safe_free(z);
}

static ssz_builder_t make_beacon_header(uint64_t slot) {
  ssz_builder_t b = ssz_builder_for_def(&LIGHT_CLIENT_HEADER[0]);
  ssz_add_uint64(&b, slot);
  ssz_add_uint64(&b, 1);
  add_zeros(&b, "parentRoot", 32);
  add_zeros(&b, "stateRoot", 32);
  add_zeros(&b, "bodyRoot", 32);
  return b;
}

static ssz_builder_t make_execution_header(void) {
  ssz_builder_t b = ssz_builder_for_def(&LIGHT_CLIENT_HEADER[1]);
  add_zeros(&b, "parentHash", 32);
  add_zeros(&b, "feeRecipient", 20);
  add_zeros(&b, "stateRoot", 32);
  add_zeros(&b, "receiptsRoot", 32);
  add_zeros(&b, "logsBloom", 256);
  add_zeros(&b, "prevRandao", 32);
  ssz_add_uint64(&b, 1);
  ssz_add_uint64(&b, 1);
  ssz_add_uint64(&b, 1);
  ssz_add_uint64(&b, 1);
  ssz_add_bytes(&b, "extraData", NULL_BYTES);
  ssz_add_uint256(&b, NULL_BYTES);
  add_zeros(&b, "blockHash", 32);
  add_zeros(&b, "transactionsRoot", 32);
  add_zeros(&b, "withdrawalsRoot", 32);
  ssz_add_uint64(&b, 0);
  ssz_add_uint64(&b, 0);
  return b;
}

static ssz_builder_t make_lc_header(uint64_t slot) {
  ssz_builder_t h = ssz_builder_for_def(&ELECTRA_LIGHT_CLIENT_UPDATE[0]);
  ssz_add_builders(&h, "beacon", make_beacon_header(slot));
  ssz_add_builders(&h, "execution", make_execution_header());
  add_zeros(&h, "executionBranch", 128);
  return h;
}

static ssz_builder_t make_sync_committee(void) {
  ssz_builder_t c = ssz_builder_for_def(&ELECTRA_LIGHT_CLIENT_UPDATE[1]);
  uint8_t*      pubs = (uint8_t*) safe_calloc(512 * 48, 1);
  pubs[0]            = 0x11;
  ssz_add_bytes(&c, "pubkeys", bytes(pubs, 512 * 48));
  safe_free(pubs);
  uint8_t agg[48] = {0};
  agg[0]          = 0x22;
  ssz_add_bytes(&c, "aggregatePubkey", bytes(agg, 48));
  return c;
}

static ssz_builder_t make_sync_aggregate(void) {
  ssz_builder_t a = ssz_builder_for_def(&ELECTRA_LIGHT_CLIENT_UPDATE[5]);
  add_zeros(&a, "syncCommitteeBits", 64);
  add_zeros(&a, "syncCommitteeSignature", 96);
  return a;
}

/** `node` is `leaf` or an ancestor of `leaf` in the generalized-index tree. */
static bool gindex_covers(gindex_t node, gindex_t leaf) {
  if (node == 0) return false;
  while (leaf > node) leaf >>= 1;
  return leaf == node;
}

static void hash_at(gindex_t node, gindex_t g_sync, const uint8_t* sync_leaf, gindex_t g_fin, const uint8_t* fin_leaf, uint8_t out[32]) {
  if (node == g_sync) {
    memcpy(out, sync_leaf, 32);
    return;
  }
  if (node == g_fin) {
    memcpy(out, fin_leaf, 32);
    return;
  }
  if (!gindex_covers(node, g_sync) && !gindex_covers(node, g_fin)) {
    memset(out, 0, 32);
    return;
  }
  uint8_t left[32], right[32];
  hash_at(node << 1, g_sync, sync_leaf, g_fin, fin_leaf, left);
  hash_at((node << 1) + 1, g_sync, sync_leaf, g_fin, fin_leaf, right);
  sha256_merkle(bytes(left, 32), bytes(right, 32), out);
}

static uint32_t fill_branch(gindex_t leaf, gindex_t g_sync, const uint8_t* sync_leaf, gindex_t g_fin, const uint8_t* fin_leaf, uint8_t* branch, uint32_t cap_bytes) {
  uint32_t n = 0;
  while (leaf > 1) {
    gindex_t sib = (leaf & 1) ? leaf - 1 : leaf + 1;
    if ((n + 1) * 32 > cap_bytes) return 0;
    hash_at(sib, g_sync, sync_leaf, g_fin, fin_leaf, branch + n * 32);
    n++;
    leaf >>= 1;
  }
  return n;
}

/**
 * Builds one Electra `light_client/updates` body.
 *
 * When `fill_proofs` is set, the finality and next-sync-committee branches are
 * rewritten so both reconstruct `attestedHeader.beacon.stateRoot` for
 * `finalized_slot`, including slot 0.
 *
 * @param period          Period the attested slot belongs to.
 * @param finalized_slot  `finalizedHeader.beacon.slot`.
 * @param fill_proofs     When false, both branches stay zero.
 * @return Wire bytes. Caller frees `data`.
 */
static bytes_t build_lcu_wire_for(uint64_t period, uint64_t finalized_slot, bool fill_proofs) {
  const uint64_t attested  = period * 8192 + 64;
  const uint64_t finalized = finalized_slot;
  ssz_builder_t  update    = ssz_builder_for_def(eth_get_light_client_update(C4_FORK_ELECTRA));
  ssz_add_builders(&update, "attestedHeader", make_lc_header(attested));
  ssz_add_builders(&update, "nextSyncCommittee", make_sync_committee());
  add_zeros(&update, "nextSyncCommitteeBranch", 6 * 32);
  ssz_add_builders(&update, "finalizedHeader", make_lc_header(finalized));
  add_zeros(&update, "finalityBranch", 7 * 32);
  ssz_add_builders(&update, "syncAggregate", make_sync_aggregate());
  ssz_add_uint64(&update, attested + 1);
  ssz_ob_t    ob = ssz_builder_to_bytes(&update);
  c4_state_t  st = {0};
  if (!ssz_is_valid(ob, true, &st)) {
    TEST_FAIL_MESSAGE(st.error ? st.error : "built light client update is not valid SSZ");
    c4_state_free(&st);
    safe_free(ob.bytes.data);
    return NULL_BYTES;
  }
  c4_state_free(&st);

  if (fill_proofs) {
    ssz_ob_t committee = ssz_get(&ob, "nextSyncCommittee");
    ssz_ob_t fin_hdr   = ssz_get(&ob, "finalizedHeader");
    ssz_ob_t fin_beacon = ssz_get(&fin_hdr, "beacon");
    bytes32_t sync_leaf = {0};
    bytes32_t fin_leaf  = {0};
    ssz_hash_tree_root(committee, sync_leaf);
    ssz_hash_tree_root(fin_beacon, fin_leaf);
    gindex_t g_sync = c4_next_sync_committee_gindex(C4_CHAIN_MAINNET, attested);
    gindex_t g_fin  = c4_finalized_root_gindex(C4_CHAIN_MAINNET, attested);
    uint8_t  sync_branch[6 * 32];
    uint8_t  fin_branch[7 * 32];
    uint8_t  root[32];
    TEST_ASSERT_EQUAL_UINT32(6, fill_branch(g_sync, g_sync, sync_leaf, g_fin, fin_leaf, sync_branch, sizeof(sync_branch)));
    TEST_ASSERT_EQUAL_UINT32(7, fill_branch(g_fin, g_sync, sync_leaf, g_fin, fin_leaf, fin_branch, sizeof(fin_branch)));
    hash_at(1, g_sync, sync_leaf, g_fin, fin_leaf, root);
    ssz_ob_t attested_hdr    = ssz_get(&ob, "attestedHeader");
    ssz_ob_t attested_beacon = ssz_get(&attested_hdr, "beacon");
    ssz_ob_t state_root      = ssz_get(&attested_beacon, "stateRoot");
    ssz_ob_t next_branch     = ssz_get(&ob, "nextSyncCommitteeBranch");
    ssz_ob_t finality_branch = ssz_get(&ob, "finalityBranch");
    TEST_ASSERT_EQUAL_UINT32(32, state_root.bytes.len);
    TEST_ASSERT_EQUAL_UINT32(sizeof(sync_branch), next_branch.bytes.len);
    TEST_ASSERT_EQUAL_UINT32(sizeof(fin_branch), finality_branch.bytes.len);
    memcpy(state_root.bytes.data, root, 32);
    memcpy(next_branch.bytes.data, sync_branch, sizeof(sync_branch));
    memcpy(finality_branch.bytes.data, fin_branch, sizeof(fin_branch));
  }

  uint8_t digest[4] = {0};
  TEST_ASSERT_TRUE(c4_eth_compute_fork_digest(C4_CHAIN_MAINNET, C4_FORK_ELECTRA, digest));
  uint32_t payload = 4 + ob.bytes.len;
  uint8_t* wire    = (uint8_t*) safe_malloc(8 + payload);
  uint64_to_le(wire, payload);
  memcpy(wire + 8, digest, 4);
  memcpy(wire + 12, ob.bytes.data, ob.bytes.len);
  safe_free(ob.bytes.data);
  return bytes(wire, 8 + payload);
}

/** `with_finality` selects a same-period finalized slot and fills both proofs. */
static bytes_t build_lcu_wire(uint64_t period, bool with_finality) {
  uint64_t finalized = with_finality ? period * 8192 + 32 : 0;
  return build_lcu_wire_for(period, finalized, with_finality);
}

/** Flips the first byte of a top-level SSZ field inside a wire body. */
static void xor_ssz_field(bytes_t wire, const char* name) {
  ssz_ob_t update = {
      .bytes = bytes(wire.data + 12, wire.len - 12),
      .def   = eth_get_light_client_update(C4_FORK_ELECTRA),
  };
  ssz_ob_t field = ssz_get(&update, name);
  TEST_ASSERT_NOT_NULL(field.bytes.data);
  TEST_ASSERT_TRUE(field.bytes.len > 0);
  field.bytes.data[0] ^= 0x01;
}

static void write_period_lcu(uint64_t period, bytes_t wire) {
  char dir[512];
  snprintf(dir, sizeof(dir), "%s/%lu", g_ps_path, (unsigned long) period);
  ensure_dir(dir);
  char path[512];
  snprintf(path, sizeof(path), "%s/" C4_PS_LCU_SSZ, dir);
  FILE* f = fopen(path, "wb");
  TEST_ASSERT_NOT_NULL(f);
  TEST_ASSERT_EQUAL(wire.len, fwrite(wire.data, 1, wire.len, f));
  fclose(f);
}

static bool period_lcu_exists(uint64_t period) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%lu/" C4_PS_LCU_SSZ, g_ps_path, (unsigned long) period);
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  fclose(f);
  return true;
}

void test_lcu_wire_rejects_without_finality(void) {
  TEST_ASSERT_FALSE(c4_ps_lcu_wire_is_cacheable(C4_CHAIN_MAINNET, NULL_BYTES, 1606));
  uint8_t short_wire[8] = {0};
  TEST_ASSERT_FALSE(c4_ps_lcu_wire_is_cacheable(C4_CHAIN_MAINNET, bytes(short_wire, sizeof(short_wire)), 1606));

  bytes_t zero_finality = build_lcu_wire(1606, false);
  TEST_ASSERT_FALSE(c4_ps_lcu_wire_is_cacheable(C4_CHAIN_MAINNET, zero_finality, 1606));
  safe_free(zero_finality.data);

  // Proofs match the slot-0 header, so this fails only the finalized-slot check.
  bytes_t proven_zero = build_lcu_wire_for(1606, 0, true);
  TEST_ASSERT_FALSE(c4_ps_lcu_wire_is_cacheable(C4_CHAIN_MAINNET, proven_zero, 1606));
  safe_free(proven_zero.data);

  bytes_t good = build_lcu_wire(1606, true);
  TEST_ASSERT_TRUE(c4_ps_lcu_wire_is_cacheable(C4_CHAIN_MAINNET, good, 1606));
  TEST_ASSERT_FALSE(c4_ps_lcu_wire_is_cacheable(C4_CHAIN_MAINNET, good, 1607));
  TEST_ASSERT_FALSE(c4_ps_lcu_wire_is_cacheable((chain_id_t) 0, good, 1606));

  bytes_t bad_len = bytes_dup(good);
  uint64_to_le(bad_len.data, (uint64_t) bad_len.len);
  TEST_ASSERT_FALSE(c4_ps_lcu_wire_is_cacheable(C4_CHAIN_MAINNET, bad_len, 1606));
  safe_free(bad_len.data);

  bytes_t bad_digest = bytes_dup(good);
  bad_digest.data[8] ^= 0xff;
  TEST_ASSERT_FALSE(c4_ps_lcu_wire_is_cacheable(C4_CHAIN_MAINNET, bad_digest, 1606));
  safe_free(bad_digest.data);

  bytes_t bad_fin = bytes_dup(good);
  xor_ssz_field(bad_fin, "finalityBranch");
  TEST_ASSERT_FALSE(c4_ps_lcu_wire_is_cacheable(C4_CHAIN_MAINNET, bad_fin, 1606));
  safe_free(bad_fin.data);

  bytes_t bad_sync = bytes_dup(good);
  xor_ssz_field(bad_sync, "nextSyncCommitteeBranch");
  TEST_ASSERT_FALSE(c4_ps_lcu_wire_is_cacheable(C4_CHAIN_MAINNET, bad_sync, 1606));
  safe_free(bad_sync.data);
  safe_free(good.data);
}

void test_period_store_lcu_cache_read(void) {
  // Both periods are still Electra on mainnet (Fulu starts at epoch 411392).
  const uint64_t period = 1605;
  const uint64_t next   = 1606;
  bytes_t        first  = build_lcu_wire(period, true);
  bytes_t        second = build_lcu_wire(next, true);
  write_period_lcu(period, first);
  write_period_lcu(next, second);

  lcu_ctx_t ctx = {0};
  c4_get_light_client_updates(&ctx, period, 2, lcu_cb);
  pump_until(&ctx.done);
  TEST_ASSERT_TRUE(ctx.done);
  TEST_ASSERT_NULL(ctx.err);
  TEST_ASSERT_EQUAL_UINT32(first.len + second.len, ctx.out.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(first.data, ctx.out.data, first.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(second.data, ctx.out.data + first.len, second.len);
  safe_free(ctx.out.data);

  const char* garbage = "LCU_PAYLOAD";
  write_period_lcu(period, bytes((uint8_t*) garbage, (uint32_t) strlen(garbage)));
  lcu_ctx_t bad = {0};
  c4_get_light_client_updates(&bad, period, 1, lcu_cb);
  pump_until(&bad.done);
  TEST_ASSERT_TRUE(bad.done);
  TEST_ASSERT_NOT_NULL(bad.err);
  TEST_ASSERT_FALSE(period_lcu_exists(period));
  TEST_ASSERT_TRUE(period_lcu_exists(next));
  safe_free(bad.err);
  safe_free(bad.out.data);
  safe_free(first.data);
  safe_free(second.data);
}

void test_period_store_lcu_cache_drops_zero_slot(void) {
  // Valid SSZ longer than the wire prefix. Empty branches are not why it is rejected:
  // the proofs match the slot-0 finalized header.
  const uint64_t period = 1604;
  bytes_t        wire   = build_lcu_wire_for(period, 0, true);
  write_period_lcu(period, wire);

  lcu_ctx_t ctx = {0};
  c4_get_light_client_updates(&ctx, period, 1, lcu_cb);
  pump_until(&ctx.done);
  TEST_ASSERT_TRUE(ctx.done);
  TEST_ASSERT_NOT_NULL(ctx.err);
  TEST_ASSERT_FALSE(period_lcu_exists(period));
  safe_free(ctx.err);
  safe_free(ctx.out.data);
  safe_free(wire.data);
}

int main(void) {
  UNITY_BEGIN();
#ifdef _MSC_VER
  fprintf(stderr, "test_server_period_store: Skipped (MSVC not supported)\n");
#else
  RUN_TEST(test_period_store_set_block_write);
  RUN_TEST(test_lcu_wire_rejects_without_finality);
  RUN_TEST(test_period_store_lcu_cache_read);
  RUN_TEST(test_period_store_lcu_cache_drops_zero_slot);
#endif
  return UNITY_END();
}

#else
int main(void) {
  fprintf(stderr, "test_server_period_store: Skipped (HTTP_SERVER not enabled)\n");
  return 0;
}
#endif
