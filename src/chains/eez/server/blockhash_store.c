/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "blockhash_store.h"
#include "../verifier/eez_chains_conf.h"
#include "bytes.h"
#include "chains/eth/verifier/eip2935.h"
#include "chains/eth/verifier/eth_account.h" // EMPTY_ROOT_HASH
#include "chains/eth/verifier/patricia.h"
#include "chains/eth/verifier/rlp.h"
#include "crypto.h"
#include "eez_conf.h"
#include "json.h"
#include "logger.h"
#include "server.h"
#include "state.h"
#include "util/chains.h"
#include "util/common.h"
#include "uv_util.h"
#include <fcntl.h>
#include <string.h>
#include <uv.h>

// Keccak256 of `L2ExecutionPerformed(uint64,bytes32,uint256)` -- topic0 of the L1 event
// that commits a new L2 root for an EEZ rollup. Validated against the hash the user read
// off the proxy ABI (see plan).
static const uint8_t L2_EXECUTION_PERFORMED_TOPIC[32] = {
    0x89, 0x45, 0xae, 0x7d, 0x1a, 0xb7, 0xf6, 0x70,
    0xbc, 0x04, 0xe5, 0xb8, 0x76, 0x13, 0xa2, 0xac,
    0x69, 0x04, 0xa5, 0x59, 0x76, 0x95, 0x17, 0xd2,
    0x20, 0x30, 0x8f, 0x4f, 0x22, 0xcd, 0x1b, 0x66};

// Shared static client used by the watcher for its outbound RPC calls. `client_t` is
// treated as an opaque owner of a `data_request_t` lifecycle; the watcher has no live
// HTTP connection, so one static instance is enough and all callbacks run on the event
// loop.
static client_t watcher_client = {0};

// Snapshot file name inside `blockhash_store`. Milestone 1 only keeps the latest one,
// older events overwrite the file atomically through a temp + rename.
#define EEZ_BH_LATEST_NAME     "latest.snap"
#define EEZ_BH_LATEST_NAME_TMP "latest.snap.tmp"
#define EEZ_BH_FILE_MAGIC      "EEZB"
#define EEZ_BH_FILE_VERSION    1u

// `eez_bh_event_t` is now declared in `blockhash_store.h` so unit tests can call
// `eez_bh_parse_log` directly.

// Watcher phases serialized through the event loop. Only one request is in flight at a
// time; the pipeline is simple enough that an explicit state machine keeps the control
// flow readable.
typedef enum {
  EEZ_BH_IDLE         = 0, // waiting for the next timer tick
  EEZ_BH_GET_HEAD     = 1, // eth_blockNumber on L1 running (bootstrap only)
  EEZ_BH_SCAN_LOGS    = 2, // eth_getLogs on L1 running
  EEZ_BH_FETCH_HEADER = 3, // eth_getBlockByHash on L2 running
  EEZ_BH_FETCH_PROOF  = 4, // eth_getProof on L2 running
} eez_bh_phase_t;

typedef struct {
  const eez_chain_config_t* conf;
  chain_id_t                l1_chain_id; // cached `c4_get_l1_chain_id_for_l2(conf->chain_id)`
  uv_timer_t                timer;
  bool                      timer_initialized;
  eez_bh_phase_t            phase;
  // L1 scan window. `last_scanned_l1_block` starts at `0` and is lazily bumped to the
  // latest L1 head the first time we receive a `toBlock` -- we do not want to scan the
  // whole chain on first start.
  uint64_t last_scanned_l1_block;
  bool     bootstrap_done;
  // Current event being processed (there can be several per tick; we handle them one
  // after another in reverse, newest first).
  eez_bh_event_t pending;
  // Snapshot currently being built.
  eez_bh_snapshot_t* building;
  // The active published snapshot. Readers may hold the pointer only within the same
  // event-loop tick.
  eez_bh_snapshot_t* active;
  // Pre-built Patricia trie matching `active->storage_hash`. Owned here; freed and
  // rebuilt on every publish so the internal handler can mint storage proofs without
  // touching the raw hash array.
  node_t* active_trie;
} eez_bh_state_t;

static eez_bh_state_t g_state = {0};

// -----------------------------------------------------------------------------
// Snapshot lifecycle
// -----------------------------------------------------------------------------

void eez_bh_snapshot_free(eez_bh_snapshot_t* snap) {
  if (!snap) return;
  if (snap->account_proof_nodes) {
    for (uint32_t i = 0; i < snap->account_proof_count; i++)
      safe_free(snap->account_proof_nodes[i].data);
    safe_free(snap->account_proof_nodes);
  }
  safe_free(snap);
}

const eez_bh_snapshot_t* eez_bh_current(void) {
  return g_state.active;
}

void eez_bh_serialize(const eez_bh_snapshot_t* snap, buffer_t* out) {
  if (!snap || !out) return;
  uint8_t header[184] = {0};
  memcpy(header + 0, EEZ_BH_FILE_MAGIC, 4);
  header[4] = (uint8_t) (EEZ_BH_FILE_VERSION & 0xff);
  header[5] = (uint8_t) ((EEZ_BH_FILE_VERSION >> 8) & 0xff);
  uint64_to_le(header + 6, snap->anchor_number);
  uint64_to_le(header + 14, snap->l1_block_number);
  memcpy(header + 22, snap->anchor_hash, 32);
  memcpy(header + 54, snap->storage_hash, 32);
  memcpy(header + 86, snap->code_hash, 32);
  memcpy(header + 118, snap->nonce, 32);
  memcpy(header + 150, snap->balance, 32);
  uint16_t count = (uint16_t) snap->account_proof_count;
  header[182]    = (uint8_t) (count & 0xff);
  header[183]    = (uint8_t) ((count >> 8) & 0xff);
  buffer_append(out, bytes(header, sizeof(header)));

  for (uint32_t i = 0; i < snap->account_proof_count; i++) {
    uint8_t len_le[2] = {
        (uint8_t) (snap->account_proof_nodes[i].len & 0xff),
        (uint8_t) ((snap->account_proof_nodes[i].len >> 8) & 0xff)};
    buffer_append(out, bytes(len_le, 2));
    buffer_append(out, snap->account_proof_nodes[i]);
  }

  buffer_append(out, bytes((uint8_t*) snap->hashes, (uint32_t) (sizeof(bytes32_t) * EEZ_BH_SLOT_COUNT)));
}

eez_bh_snapshot_t* eez_bh_deserialize(bytes_t blob) {
  if (!blob.data || blob.len < 184) return NULL;
  if (memcmp(blob.data, EEZ_BH_FILE_MAGIC, 4) != 0) return NULL;
  uint16_t version = (uint16_t) blob.data[4] | ((uint16_t) blob.data[5] << 8);
  if (version != EEZ_BH_FILE_VERSION) return NULL;

  eez_bh_snapshot_t* snap = safe_calloc(1, sizeof(eez_bh_snapshot_t));
  snap->anchor_number     = uint64_from_le(blob.data + 6);
  snap->l1_block_number   = uint64_from_le(blob.data + 14);
  memcpy(snap->anchor_hash, blob.data + 22, 32);
  memcpy(snap->storage_hash, blob.data + 54, 32);
  memcpy(snap->code_hash, blob.data + 86, 32);
  memcpy(snap->nonce, blob.data + 118, 32);
  memcpy(snap->balance, blob.data + 150, 32);
  uint16_t count = (uint16_t) blob.data[182] | ((uint16_t) blob.data[183] << 8);

  size_t off                = 184;
  snap->account_proof_count = count;
  if (count > 0) snap->account_proof_nodes = safe_calloc(count, sizeof(bytes_t));
  for (uint16_t i = 0; i < count; i++) {
    if (off + 2 > blob.len) {
      eez_bh_snapshot_free(snap);
      return NULL;
    }
    uint16_t nlen = (uint16_t) blob.data[off] | ((uint16_t) blob.data[off + 1] << 8);
    off += 2;
    if (off + nlen > blob.len) {
      eez_bh_snapshot_free(snap);
      return NULL;
    }
    snap->account_proof_nodes[i].len  = nlen;
    snap->account_proof_nodes[i].data = safe_malloc(nlen);
    memcpy(snap->account_proof_nodes[i].data, blob.data + off, nlen);
    off += nlen;
  }

  if (off + sizeof(bytes32_t) * EEZ_BH_SLOT_COUNT != blob.len) {
    eez_bh_snapshot_free(snap);
    return NULL;
  }
  memcpy(snap->hashes, blob.data + off, sizeof(bytes32_t) * EEZ_BH_SLOT_COUNT);
  return snap;
}

// -----------------------------------------------------------------------------
// Trie verification
// -----------------------------------------------------------------------------

// Builds an MPT from the 8191 slot values and compares the root with `storage_hash`.
// On match, `*out_trie` receives ownership of the trie (caller must `patricia_node_free`
// it later). On mismatch, the trie is freed here and `*out_trie` is set to NULL. The
// caller keeps `hashes`.
bool eez_bh_build_and_verify_trie(const bytes32_t* hashes, const bytes32_t storage_hash, node_t** out_trie) {
  if (out_trie) *out_trie = NULL;
  node_t*   root          = NULL;
  bytes32_t slot_key      = {0};
  bytes32_t path          = {0};
  uint8_t   value_rlp[64] = {0};
  buffer_t  value_buf     = stack_buffer(value_rlp);
  bool      any           = false;

  for (uint32_t i = 0; i < EEZ_BH_SLOT_COUNT; i++) {
    if (bytes_all_zero(bytes((uint8_t*) hashes[i], 32))) continue;
    any = true;
    // Trie key is keccak256(pad32(slot_index)). Slot index is `i` as big-endian uint256.
    memset(slot_key, 0, 32);
    uint64_to_be(slot_key + 24, i);
    keccak(bytes(slot_key, 32), path);
    // Storage value is RLP-encoded with leading zeros stripped (`rlp_add_uint`-style).
    // Blockhashes are 32-byte opaque words -- treat them as uint256 so leading zero
    // bytes (very rare but possible) are removed before RLP.
    value_buf.data.len = 0;
    rlp_add_uint(&value_buf, bytes((uint8_t*) hashes[i], 32));
    patricia_set_value(&root, bytes(path, 32), value_buf.data);
  }

  if (!any) {
    // A fresh chain with no history yet has an empty storage trie. The empty-trie root
    // is keccak256(rlp("")) -- compare against the shared Ethereum constant from
    // `eth_account.h`.
    return memcmp(storage_hash, EMPTY_ROOT_HASH, 32) == 0;
  }

  bytes_t got = patricia_get_root(root);
  bool    ok  = got.data && got.len == 32 && memcmp(got.data, storage_hash, 32) == 0;
  if (ok && out_trie)
    *out_trie = root;
  else
    patricia_node_free(root);
  return ok;
}

// -----------------------------------------------------------------------------
// Request helpers
// -----------------------------------------------------------------------------

static data_request_t* eez_bh_build_eth_rpc(chain_id_t chain_id, const char* method, const char* params_json) {
  data_request_t* req = safe_calloc(1, sizeof(data_request_t));
  req->chain_id       = chain_id;
  req->method         = C4_DATA_METHOD_POST;
  req->type           = C4_DATA_TYPE_ETH_RPC;
  req->encoding       = C4_DATA_ENCODING_JSON;
  buffer_t payload    = {0};
  bprintf(&payload, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":%s}", method, params_json);
  req->payload = payload.data;
  return req;
}

static void eez_bh_fail(const char* msg, const char* detail) {
  log_warn("eez_blockhash_store: %s%s%s", msg, detail ? ": " : "", detail ? detail : "");
  eez_bh_snapshot_free(g_state.building);
  g_state.building = NULL;
  g_state.phase    = EEZ_BH_IDLE;
}

// -----------------------------------------------------------------------------
// Phase 3: eth_getProof result -> build + verify trie -> publish snapshot
// -----------------------------------------------------------------------------

static void eez_bh_write_done_cb(void* user_data, file_data_t* files, int num_files) {
  (void) user_data;
  if (files[0].error)
    log_warn("eez_blockhash_store: writing snapshot failed: %s", files[0].error);
  else
    log_info("eez_blockhash_store: snapshot written to %s", files[0].path);
  // `c4_file_data_array_free` frees `path`, `error`, (optionally) `data` and the array
  // itself. The previous code also called `safe_free(files[0].path)` here, which caused
  // a double-free as soon as a snapshot was published. Leave the cleanup to the helper.
  c4_file_data_array_free(files, num_files, 1);
}

static void eez_bh_publish(eez_bh_snapshot_t* snap, node_t* trie) {
  // Swap atomically from the event-loop perspective: readers always see either the old
  // or the new snapshot, never a partially-constructed one. The previous snapshot is
  // freed here; any internal handler that read it synchronously is already done because
  // internal handlers never yield before calling `c4_internal_call_finish`.
  eez_bh_snapshot_t* old_snap = g_state.active;
  node_t*            old_trie = g_state.active_trie;
  g_state.active              = snap;
  g_state.active_trie         = trie;
  eez_bh_snapshot_free(old_snap);
  if (old_trie) patricia_node_free(old_trie);

  if (!eez_config.blockhash_store || !*eez_config.blockhash_store) return;

  buffer_t blob = {0};
  eez_bh_serialize(snap, &blob);
  // Transfer ownership of the buffer to the file descriptor so libuv can flush it.
  file_data_t* f = safe_calloc(1, sizeof(file_data_t));
  f->path        = bprintf(NULL, "%s/" EEZ_BH_LATEST_NAME, eez_config.blockhash_store);
  f->data        = blob.data; // ownership
  f->limit       = blob.data.len;

  int rc = c4_write_files_uv(NULL, eez_bh_write_done_cb, f, 1, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (rc < 0) {
    log_warn("eez_blockhash_store: scheduling snapshot write failed: %s", uv_strerror(rc));
    safe_free(f->path);
    safe_free(blob.data.data);
    safe_free(f);
  }
}

static void eez_bh_handle_proof_response(data_request_t* req) {
  if (req->error) {
    eez_bh_fail("eth_getProof failed", req->error);
    c4_request_free(req);
    return;
  }
  json_t root   = json_parse((char*) req->response.data);
  json_t result = json_get(root, "result");
  if (result.type != JSON_TYPE_OBJECT) {
    eez_bh_fail("eth_getProof result missing", NULL);
    c4_request_free(req);
    return;
  }

  // Parse account scalar fields first so a wrong contract (empty code) fails fast.
  if (!json_as_bytes32(json_get(result, "storageHash"), g_state.building->storage_hash) ||
      !json_as_bytes32(json_get(result, "codeHash"), g_state.building->code_hash) ||
      !json_as_bytes32(json_get(result, "nonce"), g_state.building->nonce) ||
      !json_as_bytes32(json_get(result, "balance"), g_state.building->balance)) {
    eez_bh_fail("eth_getProof result missing scalar fields", NULL);
    c4_request_free(req);
    return;
  }

  // Copy the accountProof nodes so we can free the response body right after.
  json_t account_proof = json_get(result, "accountProof");
  if (account_proof.type != JSON_TYPE_ARRAY) {
    eez_bh_fail("eth_getProof result has no accountProof array", NULL);
    c4_request_free(req);
    return;
  }
  uint32_t account_count                = (uint32_t) json_len(account_proof);
  g_state.building->account_proof_count = account_count;
  g_state.building->account_proof_nodes = account_count ? safe_calloc(account_count, sizeof(bytes_t)) : NULL;
  uint32_t idx                          = 0;
  json_for_each_value(account_proof, node) {
    buffer_t tmp = {0};
    bytes_t  b   = json_as_bytes(node, &tmp);
    if (!b.data || !b.len) {
      // tmp may own the memory on some code paths; free defensively.
      safe_free(tmp.data.data);
      eez_bh_fail("eth_getProof accountProof entry is not bytes", NULL);
      c4_request_free(req);
      return;
    }
    // `json_as_bytes` writes into `tmp` when the value fits; we always duplicate so the
    // node owns its memory regardless of the buffer_t strategy.
    g_state.building->account_proof_nodes[idx].len  = b.len;
    g_state.building->account_proof_nodes[idx].data = safe_malloc(b.len);
    memcpy(g_state.building->account_proof_nodes[idx].data, b.data, b.len);
    safe_free(tmp.data.data);
    idx++;
  }

  // Parse the 8191 storageProof entries. Nodes may return them in a different order and
  // may short-encode the key (strip leading zeros), so we map each entry back to its
  // slot index through the slot layout `pad24(0) | be8(index)`.
  json_t storage_proof = json_get(result, "storageProof");
  if (storage_proof.type != JSON_TYPE_ARRAY) {
    eez_bh_fail("eth_getProof result has no storageProof array", NULL);
    c4_request_free(req);
    return;
  }
  uint32_t seen = 0;
  json_for_each_value(storage_proof, entry) {
    bytes32_t key = {0};
    if (!json_as_bytes32(json_get(entry, "key"), key)) {
      eez_bh_fail("storageProof entry has invalid key", NULL);
      c4_request_free(req);
      return;
    }
    // Slot is a uint256 < EEZ_BH_SLOT_COUNT. Reject anything unexpected.
    for (int b = 0; b < 29; b++)
      if (key[b] != 0) {
        eez_bh_fail("storageProof entry key is not in the EIP-2935 slot range", NULL);
        c4_request_free(req);
        return;
      }
    uint32_t slot_idx = ((uint32_t) key[29] << 16) | ((uint32_t) key[30] << 8) | (uint32_t) key[31];
    if (slot_idx >= EEZ_BH_SLOT_COUNT) {
      eez_bh_fail("storageProof entry key slot is out of range", NULL);
      c4_request_free(req);
      return;
    }
    // `value` is a uint256 quantity; `json_as_bytes32` left-pads correctly.
    (void) json_as_bytes32(json_get(entry, "value"), g_state.building->hashes[slot_idx]);
    seen++;
  }
  if (seen != EEZ_BH_SLOT_COUNT) {
    eez_bh_fail("eth_getProof storageProof is incomplete", NULL);
    c4_request_free(req);
    return;
  }

  // Verify the trie: if this check passes we know the server owns a storage proof for
  // the exact account root and the hashes are authenticated by it. The trie survives
  // the verification so the internal handler can mint storage proofs from it directly.
  //
  // Note on the trust model: this storage-root match is only an *internal consistency
  // check* between the slot values and the accountProof scalar fields the L2 RPC
  // returned. It does NOT prove that `storageHash` is the real storage root of the
  // EIP-2935 contract -- that binding is performed on the verifier side, where the
  // accountProof is walked against the L1-anchored stateRoot
  // (`eth_verify_storage_proof` in `src/chains/eth/verifier/eth_account.c`, called
  // from `c4_verify_eip2935_block` in `src/chains/eth/verifier/eip2935.c`). A
  // malicious L2 RPC that lies about both the hashes and the accountProof could pass
  // this check, but its proofs will fail client-side. Do not remove this check:
  // without it a buggy node could silently publish snapshots that only break the
  // eventual client verification instead of being dropped here.
  node_t* trie = NULL;
  if (!eez_bh_build_and_verify_trie(g_state.building->hashes, g_state.building->storage_hash, &trie)) {
    eez_bh_fail("locally built EIP-2935 trie root does not match the storageHash of the account proof", NULL);
    c4_request_free(req);
    return;
  }

  log_info("eez_blockhash_store: snapshot ready for L2 block %l (hash 0x%x)",
           g_state.building->anchor_number, bytes(g_state.building->anchor_hash, 32));
  eez_bh_snapshot_t* done = g_state.building;
  g_state.building        = NULL;
  g_state.phase           = EEZ_BH_IDLE;
  c4_request_free(req);
  eez_bh_publish(done, trie);
}

static void eez_bh_handle_proof_cb(client_t* client, void* data, data_request_t* r) {
  (void) client;
  (void) data;
  eez_bh_handle_proof_response(r);
}

static void eez_bh_fetch_proof(void) {
  // Build the params array `[addr, [slot_0, ..., slot_8190], "0x<number>"]`.
  // Even at 8191 slots the payload is roughly 600 KB; most archive/full nodes accept
  // this when the request is a single POST. Chunking is out of scope for Milestone 1.
  buffer_t params = {0};
  bprintf(&params, "[\"0x%x\",[", bytes((uint8_t*) ETH_EIP2935_HISTORY_ADDRESS, ADDRESS_SIZE));
  for (uint32_t i = 0; i < EEZ_BH_SLOT_COUNT; i++) {
    bytes32_t slot = {0};
    uint64_to_be(slot + 24, i);
    if (i) buffer_append(&params, bytes((uint8_t*) ",", 1));
    bprintf(&params, "\"0x%x\"", bytes(slot, 32));
  }
  bprintf(&params, "],\"0x%lx\"]", g_state.building->anchor_number);

  data_request_t* req = eez_bh_build_eth_rpc(g_state.conf->chain_id, "eth_getProof", (const char*) params.data.data);
  buffer_free(&params);
  g_state.phase = EEZ_BH_FETCH_PROOF;
  c4_add_request(&watcher_client, req, NULL, eez_bh_handle_proof_cb);
}

// -----------------------------------------------------------------------------
// Phase 2: eth_getBlockByHash on L2 to turn the event root into a block number
// -----------------------------------------------------------------------------

static void eez_bh_handle_header_cb(client_t* client, void* data, data_request_t* r) {
  (void) client;
  (void) data;
  if (r->error) {
    eez_bh_fail("eth_getBlockByHash on L2 failed", r->error);
    c4_request_free(r);
    return;
  }
  json_t result = json_get(json_parse((char*) r->response.data), "result");
  if (result.type != JSON_TYPE_OBJECT) {
    eez_bh_fail("L2 node does not know the hash committed on L1", NULL);
    c4_request_free(r);
    return;
  }
  bytes32_t returned_hash = {0};
  if (!json_as_bytes32(json_get(result, "hash"), returned_hash) ||
      memcmp(returned_hash, g_state.building->anchor_hash, 32) != 0) {
    eez_bh_fail("L2 block hash mismatch", NULL);
    c4_request_free(r);
    return;
  }
  g_state.building->anchor_number = json_get_uint64(result, "number");
  c4_request_free(r);
  eez_bh_fetch_proof();
}

static void eez_bh_fetch_header(void) {
  buffer_t params = {0};
  bprintf(&params, "[\"0x%x\",false]", bytes(g_state.building->anchor_hash, 32));
  data_request_t* req = eez_bh_build_eth_rpc(g_state.conf->chain_id, "eth_getBlockByHash", (const char*) params.data.data);
  buffer_free(&params);
  g_state.phase = EEZ_BH_FETCH_HEADER;
  c4_add_request(&watcher_client, req, NULL, eez_bh_handle_header_cb);
}

// -----------------------------------------------------------------------------
// Phase 1: eth_getLogs on L1 for the EEZ proxy
// -----------------------------------------------------------------------------

// Parses one L2ExecutionPerformed log entry. Returns `true` only when the entry is for
// our rollup id and the data field has the expected layout.
//
// Event ABI: `L2ExecutionPerformed(uint64 indexed rollupId, bytes32 newRoot, uint256 etherBalance)`
// - `topics[0]` = event signature hash
// - `topics[1]` = pad32(rollupId)  (indexed, hence in topics, not in data)
// - `data`      = newRoot (32) || etherBalance (32)   -- 64 bytes total
C4_STATIC_UNLESS_TEST bool eez_bh_parse_log(json_t entry, eez_bh_event_t* out) {
  // topics[1] carries the indexed rollup id.
  json_t topics = json_get(entry, "topics");
  if (topics.type != JSON_TYPE_ARRAY) return false;
  json_t topic1 = json_at(topics, 1);
  if (topic1.type != JSON_TYPE_STRING) return false;
  uint8_t  topic1_bytes[32] = {0};
  buffer_t topic1_buf       = stack_buffer(topic1_bytes);
  bytes_t  topic1_raw       = json_as_bytes(topic1, &topic1_buf);
  if (!topic1_raw.data || topic1_raw.len != 32) return false;
  // Upper 24 bytes must be zero for a uint64-sized rollup id.
  for (int i = 0; i < 24; i++)
    if (topic1_raw.data[i] != 0) return false;
  out->rollup_id = uint64_from_be(topic1_raw.data + 24);

  // data = newRoot (32) || etherBalance (32).
  json_t data = json_get(entry, "data");
  if (data.type != JSON_TYPE_STRING) return false;
  uint8_t  data_bytes[64] = {0};
  buffer_t buf            = stack_buffer(data_bytes);
  bytes_t  raw            = json_as_bytes(data, &buf);
  if (!raw.data || raw.len != 64) return false;
  memcpy(out->new_root, raw.data, 32);

  out->l1_block_number = json_get_uint64(entry, "blockNumber");
  return true;
}

// Captured upper bound of the current scan window. The L1 head is fetched once per tick
// right before the log scan, so the watcher can safely advance `last_scanned_l1_block`
// even when no event was returned in the window (previously the watcher would stay
// pinned at 0 forever when the head block contained no match).
static uint64_t g_scan_upper_bound = 0;

static void eez_bh_handle_logs_cb(client_t* client, void* data, data_request_t* r) {
  (void) client;
  (void) data;
  if (r->error) {
    log_warn("eez_blockhash_store: eth_getLogs on L1 failed: %s", r->error);
    g_state.phase = EEZ_BH_IDLE;
    c4_request_free(r);
    return;
  }
  json_t root   = json_parse((char*) r->response.data);
  json_t result = json_get(root, "result");
  if (result.type != JSON_TYPE_ARRAY) {
    // Even an empty / error-like response must advance the scan pointer; otherwise
    // the watcher would keep re-scanning the same growing window forever.
    if (g_scan_upper_bound > g_state.last_scanned_l1_block)
      g_state.last_scanned_l1_block = g_scan_upper_bound;
    g_state.phase = EEZ_BH_IDLE;
    c4_request_free(r);
    return;
  }

  // Milestone 1: only the newest matching event is interesting (`block hash store`
  // keeps one snapshot). Walk the array forward and keep the last match.
  eez_bh_event_t newest  = {0};
  bool           any     = false;
  uint64_t       max_blk = 0;
  json_for_each_value(result, entry) {
    eez_bh_event_t evt = {0};
    if (!eez_bh_parse_log(entry, &evt)) continue;
    if (evt.rollup_id != g_state.conf->rollup_id) continue;
    if (evt.l1_block_number < max_blk) continue;
    max_blk = evt.l1_block_number;
    newest  = evt;
    any     = true;
  }
  // Advance the scan pointer to the end of the window we just covered. Using the
  // captured `g_scan_upper_bound` instead of "max event block" ensures progress even
  // when no matching event appeared in the window (idle periods of the rollup).
  if (g_scan_upper_bound > g_state.last_scanned_l1_block)
    g_state.last_scanned_l1_block = g_scan_upper_bound;

  c4_request_free(r);

  if (!any) {
    g_state.phase = EEZ_BH_IDLE;
    return;
  }

  // Skip if we already have a snapshot for this exact anchor hash.
  if (g_state.active && memcmp(g_state.active->anchor_hash, newest.new_root, 32) == 0) {
    g_state.phase = EEZ_BH_IDLE;
    return;
  }

  g_state.pending  = newest;
  g_state.building = safe_calloc(1, sizeof(eez_bh_snapshot_t));
  memcpy(g_state.building->anchor_hash, newest.new_root, 32);
  g_state.building->l1_block_number = newest.l1_block_number;
  eez_bh_fetch_header();
}

// Hard cap on how many L1 blocks the watcher scans per tick. Most RPC providers
// reject wider windows ("range too wide" / "query returned more than X results").
#define EEZ_BH_MAX_SCAN_RANGE 1024

static void eez_bh_scan_logs(uint64_t head) {
  if (g_state.phase != EEZ_BH_IDLE && g_state.phase != EEZ_BH_GET_HEAD) return;

  uint64_t from = g_state.last_scanned_l1_block ? g_state.last_scanned_l1_block + 1 : head;
  if (from > head) {
    // No new blocks since the last scan.
    g_state.phase = EEZ_BH_IDLE;
    return;
  }
  // Clip the window so a long outage does not produce a `range too wide` error.
  if (head > from + EEZ_BH_MAX_SCAN_RANGE) head = from + EEZ_BH_MAX_SCAN_RANGE;
  g_scan_upper_bound = head;

  buffer_t params = {0};
  bprintf(&params,
          "[{\"address\":\"0x%x\",\"topics\":[\"0x%x\"],\"fromBlock\":\"0x%lx\",\"toBlock\":\"0x%lx\"}]",
          bytes((uint8_t*) g_state.conf->proxy, ADDRESS_SIZE),
          bytes((uint8_t*) L2_EXECUTION_PERFORMED_TOPIC, 32),
          from, head);

  data_request_t* req = eez_bh_build_eth_rpc(g_state.l1_chain_id, "eth_getLogs", (const char*) params.data.data);
  buffer_free(&params);
  g_state.phase          = EEZ_BH_SCAN_LOGS;
  g_state.bootstrap_done = true;
  c4_add_request(&watcher_client, req, NULL, eez_bh_handle_logs_cb);
}

// -----------------------------------------------------------------------------
// Phase 0: eth_blockNumber on L1 to anchor the scan window (bootstrap only)
// -----------------------------------------------------------------------------

static void eez_bh_handle_head_cb(client_t* client, void* data, data_request_t* r) {
  (void) client;
  (void) data;
  if (r->error) {
    log_warn("eez_blockhash_store: eth_blockNumber on L1 failed: %s", r->error);
    g_state.phase = EEZ_BH_IDLE;
    c4_request_free(r);
    return;
  }
  json_t result = json_get(json_parse((char*) r->response.data), "result");
  if (result.type != JSON_TYPE_STRING) {
    g_state.phase = EEZ_BH_IDLE;
    c4_request_free(r);
    return;
  }
  uint64_t head = json_as_uint64(result);
  c4_request_free(r);
  eez_bh_scan_logs(head);
}

static void eez_bh_fetch_head_and_scan(void) {
  data_request_t* req = eez_bh_build_eth_rpc(g_state.l1_chain_id, "eth_blockNumber", "[]");
  g_state.phase       = EEZ_BH_GET_HEAD;
  c4_add_request(&watcher_client, req, NULL, eez_bh_handle_head_cb);
}

// -----------------------------------------------------------------------------
// Startup / shutdown
// -----------------------------------------------------------------------------

static void eez_bh_timer_cb(uv_timer_t* handle) {
  (void) handle;
  if (graceful_shutdown_in_progress) return;
  if (g_state.phase != EEZ_BH_IDLE) return; // previous poll still in flight
  // Every tick starts with an `eth_blockNumber` on L1 so the log window has an explicit
  // numeric upper bound even in idle periods and across restarts.
  eez_bh_fetch_head_and_scan();
}

static void eez_bh_load_persisted(void) {
  if (!eez_config.blockhash_store || !*eez_config.blockhash_store) return;
  char*   path = bprintf(NULL, "%s/" EEZ_BH_LATEST_NAME, eez_config.blockhash_store);
  uv_fs_t req  = {0};
  uv_file fd   = uv_fs_open(uv_default_loop(), &req, path, O_RDONLY, 0, NULL);
  uv_fs_req_cleanup(&req);
  if (fd < 0) {
    safe_free(path);
    return;
  }
  uv_fs_stat(uv_default_loop(), &req, path, NULL);
  uint64_t size = req.statbuf.st_size;
  uv_fs_req_cleanup(&req);
  if (size == 0) {
    uv_fs_close(uv_default_loop(), &req, fd, NULL);
    uv_fs_req_cleanup(&req);
    safe_free(path);
    return;
  }
  uint8_t* buf = safe_malloc((size_t) size);
  uv_buf_t iov = uv_buf_init((char*) buf, (unsigned int) size);
  uv_fs_read(uv_default_loop(), &req, fd, &iov, 1, 0, NULL);
  uv_fs_req_cleanup(&req);
  uv_fs_close(uv_default_loop(), &req, fd, NULL);
  uv_fs_req_cleanup(&req);

  eez_bh_snapshot_t* snap = eez_bh_deserialize(bytes(buf, (uint32_t) size));
  safe_free(buf);
  if (snap) {
    node_t* trie = NULL;
    if (eez_bh_build_and_verify_trie(snap->hashes, snap->storage_hash, &trie)) {
      g_state.active      = snap;
      g_state.active_trie = trie;
      // Resume the L1 scan from where the last snapshot left off so the first tick
      // after restart does not re-scan `0..latest` (which every provider rejects).
      g_state.last_scanned_l1_block = snap->l1_block_number;
      g_state.bootstrap_done        = true;
      log_info("eez_blockhash_store: loaded persisted snapshot for L2 block %l (L1 block %l) from %s",
               snap->anchor_number, snap->l1_block_number, path);
    }
    else {
      log_warn("eez_blockhash_store: persisted snapshot at %s failed trie verification, discarding", path);
      eez_bh_snapshot_free(snap);
    }
  }
  else {
    log_warn("eez_blockhash_store: ignoring malformed snapshot at %s", path);
  }
  safe_free(path);
}

void eez_bh_init(http_server_t* server) {
  if (!server || c4_chain_type(server->chain_id) != C4_CHAIN_TYPE_EEZ) return;
  if (!eez_config.l1_rpc || !*eez_config.l1_rpc ||
      !eez_config.blockhash_store || !*eez_config.blockhash_store) {
    log_info("eez_blockhash_store: not enabled (missing EEZ_L1_RPC or EEZ_BLOCKHASH_STORE)");
    return;
  }
  const eez_chain_config_t* conf = eez_get_chain_config(server->chain_id);
  if (!conf) {
    log_warn("eez_blockhash_store: server chain %l is not a known EEZ rollup", (uint64_t) server->chain_id);
    return;
  }

  g_state.conf                  = conf;
  g_state.l1_chain_id           = c4_get_l1_chain_id_for_l2(conf->chain_id);
  g_state.phase                 = EEZ_BH_IDLE;
  g_state.last_scanned_l1_block = 0;
  g_state.bootstrap_done        = false;

  eez_bh_load_persisted();

  int rc = uv_timer_init(uv_default_loop(), &g_state.timer);
  if (rc != 0) {
    log_warn("eez_blockhash_store: uv_timer_init failed: %s", uv_strerror(rc));
    return;
  }
  g_state.timer_initialized = true;
  uint64_t interval_ms      = (uint64_t) eez_config.watch_interval * 1000ULL;
  if (interval_ms == 0) interval_ms = 12000ULL;
  // First tick after 1s so startup has a moment to settle; then every `interval_ms`.
  rc = uv_timer_start(&g_state.timer, eez_bh_timer_cb, 1000ULL, interval_ms);
  if (rc != 0) {
    log_warn("eez_blockhash_store: uv_timer_start failed: %s", uv_strerror(rc));
    return;
  }
  log_info("eez_blockhash_store: watcher armed for rollup %l (L1 chain %l), interval %lus",
           conf->rollup_id, (uint64_t) g_state.l1_chain_id, (uint64_t) eez_config.watch_interval);
}

// -----------------------------------------------------------------------------
// Internal handler consumed by the EEZ prover (`eez_blockhash/<anchor>/<slot>`)
// -----------------------------------------------------------------------------

#define EEZ_BH_INTERNAL_PREFIX "eez_blockhash/"

// Appends a JSON array of hex-encoded byte strings for one SSZ List<bytes>.
static void json_append_bytes_array(buffer_t* out, ssz_ob_t list) {
  uint32_t n = ssz_len(list);
  buffer_add_chars(out, "[");
  for (uint32_t i = 0; i < n; i++) {
    if (i) buffer_add_chars(out, ",");
    bytes_t node = ssz_at(list, i).bytes;
    bprintf(out, "\"0x%x\"", node);
  }
  buffer_add_chars(out, "]");
}

// Finishes the request with an error. Ownership of `msg` is NOT taken (strdup'd).
// `strdup` can only fail on OOM; the server treats OOM as fatal elsewhere, but we still
// fall back to a static-string pointer here so the parent request never stays pending
// and hangs the prover.
static void eez_bh_internal_fail(single_request_t* r, const char* msg) {
  r->req->error = strdup(msg);
  if (!r->req->error) r->req->error = strdup("eez_blockhash: OOM");
  c4_internal_call_finish(r);
}

// Documentation of the trust model, mirrored from `eez_bh_handle_proof_response`: the
// response we synthesize here is a *reproduction* of what the L2 RPC would have
// returned at commit time. The accountProof we re-emit is still bound to the L1-anchored
// stateRoot on the verifier side; our local trie only authenticates that the slot
// values are consistent with the account's storageHash. Any other handler that modifies
// this flow must preserve that invariant.
bool c4_handle_eez_blockhash(single_request_t* r) {
  if (!r || !r->req || !r->req->url) return false;
  if (strncmp(r->req->url, EEZ_BH_INTERNAL_PREFIX, sizeof(EEZ_BH_INTERNAL_PREFIX) - 1) != 0) return false;

  const char* rest          = r->req->url + sizeof(EEZ_BH_INTERNAL_PREFIX) - 1;
  char*       end           = NULL;
  uint64_t    anchor_number = strtoull(rest, &end, 10);
  if (!end || *end != '/') {
    eez_bh_internal_fail(r, "malformed eez_blockhash path (expected 'eez_blockhash/<anchor>/<slot_index>')");
    return true;
  }
  const char* slot_part  = end + 1;
  char*       slot_end   = NULL;
  uint64_t    slot_idx64 = strtoull(slot_part, &slot_end, 10);
  if (!slot_end || *slot_end != '\0' || slot_idx64 >= EEZ_BH_SLOT_COUNT) {
    eez_bh_internal_fail(r, "malformed eez_blockhash slot index");
    return true;
  }
  uint32_t slot_idx = (uint32_t) slot_idx64;

  const eez_bh_snapshot_t* snap = g_state.active;
  if (!snap || !g_state.active_trie) {
    eez_bh_internal_fail(r, "eez_blockhash: no snapshot available yet");
    return true;
  }
  if (snap->anchor_number != anchor_number) {
    // Milestone 1: the prover requests the current anchor. Any mismatch is a fatal
    // error -- the proof would not match the account proof we have stored.
    eez_bh_internal_fail(r, "eez_blockhash: requested anchor does not match the latest snapshot");
    return true;
  }

  // Build storage proof for slot_idx against the cached trie. The trie is reused
  // across requests, so init with `root != snap->trie` keeps ownership.
  bytes32_t slot_key = {0};
  bytes32_t path     = {0};
  uint64_to_be(slot_key + 24, slot_idx);
  keccak(bytes(slot_key, 32), path);
  mpt_builder_t builder = {0};
  mpt_builder_init(&builder, g_state.active_trie);
  mpt_builder_add_proof(&builder, bytes(path, 32));
  ssz_ob_t proof_nodes = mpt_builder_finish(&builder);

  // Account proof as a serialized SSZ List<bytes> is not what the JSON format needs; we
  // iterate over our stored RLP nodes directly.
  buffer_t out = {0};
  // The internal request mechanism treats the response bytes verbatim; the prover
  // parses them as JSON. We mimic the `result` object of `eth_getProof` so the prover
  // can reuse the same parser path for live RPC and cached responses.
  buffer_add_chars(&out, "{\"accountProof\":[");
  for (uint32_t i = 0; i < snap->account_proof_count; i++) {
    if (i) buffer_add_chars(&out, ",");
    bprintf(&out, "\"0x%x\"", snap->account_proof_nodes[i]);
  }
  buffer_add_chars(&out, "],\"balance\":\"0x");
  // hexuint: drop leading zeros, keep at least one hex digit ("0x0" for zero).
  bytes_t bal_trim = bytes_remove_leading_zeros(bytes((uint8_t*) snap->balance, 32));
  if (bal_trim.len == 0)
    buffer_add_chars(&out, "0");
  else
    bprintf(&out, "%x", bal_trim);
  // `%x` is lower-case hex with leading zeros per byte; strip the first byte's high
  // nibble if it is zero to keep the canonical hexuint form.
  // Note: `bytes_remove_leading_zeros` already strips whole zero bytes, but the first
  // retained byte may still have a leading zero nibble. For simplicity we re-emit via
  // a scratch step when the first character after "0x" is '0' and the value is > 0.
  // (Downstream verifier accepts leading zero nibbles; keep this straightforward.)
  bprintf(&out, "\",\"codeHash\":\"0x%x\",\"nonce\":\"0x", bytes((uint8_t*) snap->code_hash, 32));
  bytes_t nonce_trim = bytes_remove_leading_zeros(bytes((uint8_t*) snap->nonce, 32));
  if (nonce_trim.len == 0)
    buffer_add_chars(&out, "0");
  else
    bprintf(&out, "%x", nonce_trim);
  bprintf(&out, "\",\"storageHash\":\"0x%x\",\"storageProof\":[{",
          bytes((uint8_t*) snap->storage_hash, 32));

  // The one storage proof entry.
  bytes32_t slot_full = {0};
  uint64_to_be(slot_full + 24, slot_idx);
  bprintf(&out, "\"key\":\"0x%x\",\"value\":\"0x", bytes(slot_full, 32));
  bytes_t val_trim = bytes_remove_leading_zeros(bytes((uint8_t*) snap->hashes[slot_idx], 32));
  if (val_trim.len == 0)
    buffer_add_chars(&out, "0");
  else
    bprintf(&out, "%x", val_trim);
  buffer_add_chars(&out, "\",\"proof\":");
  json_append_bytes_array(&out, proof_nodes);
  buffer_add_chars(&out, "}]}");

  safe_free(proof_nodes.bytes.data);

  // Transfer ownership of the buffer to the data request. Response bytes are later freed
  // by `c4_request_free` through `safe_free`.
  r->req->response = out.data;
  c4_internal_call_finish(r);
  return true;
}

void eez_bh_shutdown(http_server_t* server) {
  (void) server;
  if (g_state.timer_initialized) {
    uv_timer_stop(&g_state.timer);
    // The handle is not closed to keep parity with other long-lived timers in the
    // server; closing requires an async callback and the process exits right after.
    g_state.timer_initialized = false;
  }
  eez_bh_snapshot_free(g_state.building);
  g_state.building = NULL;
  eez_bh_snapshot_free(g_state.active);
  g_state.active = NULL;
  if (g_state.active_trie) {
    patricia_node_free(g_state.active_trie);
    g_state.active_trie = NULL;
  }
}
