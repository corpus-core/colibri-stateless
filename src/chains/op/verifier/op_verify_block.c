/*
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "beacon_types.h"
#include "bytes.h"
#include "crypto.h"
#include "el_header.h"
#include "eth_verify.h"
#include "header_cache.h"
#include "op_chains_conf.h"
#include "op_verify.h"
#include "op_zstd.h"
#include "ssz.h"
#include "state.h"
#include <stdbool.h>
#include <string.h>

#define OP_ZSTD_MAX_SIZE (32u * 1024u * 1024u)

// Superchain-default header layout vs OP forks (keccak match is authoritative):
//   Ecotone..Holocene (< 2025-05-09): Deneb RLP, prefix = parentBeaconRoot
//   Isthmus (2025-05-09), Jovian (2025-12-02), Karst (2026-07-08): Electra RLP
//     (requestsHash = sha256('') per OP spec unless the 64-byte prefix supplies it)
//   Gloas-equivalent: not scheduled on OP; 72-byte prefix carries slot if it appears.

// Isthmus ExecutionPayload = Deneb fields + withdrawalsRoot (L2ToL1MessagePasser storage root).
#define OP_ISTHMUS_WITHDRAWALS_LIMIT 16
static const ssz_def_t ISTHMUS_EXECUTION_PAYLOAD[] = {
    SSZ_BYTES32("parentHash"),
    SSZ_ADDRESS("feeRecipient"),
    SSZ_BYTES32("stateRoot"),
    SSZ_BYTES32("receiptsRoot"),
    SSZ_BYTE_VECTOR("logsBloom", 256),
    SSZ_BYTES32("prevRandao"),
    SSZ_UINT64("blockNumber"),
    SSZ_UINT64("gasLimit"),
    SSZ_UINT64("gasUsed"),
    SSZ_UINT64("timestamp"),
    SSZ_BYTES("extraData", 32),
    SSZ_UINT256("baseFeePerGas"),
    SSZ_BYTES32("blockHash"),
    SSZ_LIST("transactions", ssz_transactions_bytes, 1048576),
    SSZ_LIST("withdrawals", DENEP_WITHDRAWAL_CONTAINER, OP_ISTHMUS_WITHDRAWALS_LIMIT),
    SSZ_UINT64("blobGasUsed"),
    SSZ_UINT64("excessBlobGas"),
    SSZ_BYTES32("withdrawalsRoot"),
};

static const ssz_def_t DENEB_EXECUTION_PAYLOAD_CONTAINER   = SSZ_CONTAINER("payload", DENEP_EXECUTION_PAYLOAD);
static const ssz_def_t ISTHMUS_EXECUTION_PAYLOAD_CONTAINER = SSZ_CONTAINER("payload", ISTHMUS_EXECUTION_PAYLOAD);

// Extra EL-header fields that are not in the Deneb SSZ payload are prepended:
//   Deneb/Ecotone:  [parentBeaconRoot(32) | payload]
//   Electra/Isthmus:[parentBeaconRoot(32) | requestsHash(32) | payload]
//   Gloas:          [parentBeaconRoot(32) | requestsHash(32) | slot(8 LE) | payload]
// A legacy 32-byte prefix is still accepted for Electra: requestsHash defaults to sha256('').
typedef struct {
  uint32_t prefix_len;
  bool     has_requests_hash;
  bool     has_slot;
} op_preconf_layout_t;

static const op_preconf_layout_t OP_PRECONF_LAYOUTS[] = {
    {72, true, true},
    {64, true, false},
    {32, false, false},
};

static bool recover_sequencer(bytes_t data, bytes_t signature, uint64_t chain_id, address_t address) {
  uint8_t buf[96] = {0};
  uint8_t pub[64] = {0};
  uint64_to_be(buf + 64 - 8, chain_id);
  keccak(data, buf + 64);
  keccak(bytes(buf, 96), buf);
  if (!secp256k1_recover(buf, signature, pub)) return false;
  keccak(bytes(pub, 64), buf);
  memcpy(address, buf + 12, 20);
  return true;
}

c4_status_t op_decompress_preconf(c4_state_t* state, bytes_t compressed, bytes_t* out) {
  size_t expected = op_zstd_get_decompressed_size(compressed);
  if (expected == 0 || expected > OP_ZSTD_MAX_SIZE) {
    if (state) c4_state_add_error(state, "failed to get decompressed size");
    return C4_ERROR;
  }
  bytes_t buf    = bytes(safe_malloc(expected), (uint32_t) expected);
  size_t  actual = op_zstd_decompress(compressed, buf);
  if (actual != expected) {
    safe_free(buf.data);
    if (state) c4_state_add_error(state, "failed to decompress data");
    return C4_ERROR;
  }
  *out = buf;
  return C4_SUCCESS;
}

static void op_empty_requests_hash(bytes32_t out) {
  sha256(NULL_BYTES, out);
}

// extraData is the first dynamic field. Its offset equals the container's
// fixed length: 528 (Deneb) or 560 (Isthmus, + withdrawalsRoot).
#define OP_EP_FIRST_OFFSET_AT 436u
#define OP_EP_DENEB_FIXED     528u
#define OP_EP_ISTHMUS_FIXED   560u

static bool payload_fixed_len_is(bytes_t raw, uint32_t expected) {
  if (raw.len < OP_EP_FIRST_OFFSET_AT + 4) return false;
  return uint32_from_le(raw.data + OP_EP_FIRST_OFFSET_AT) == expected;
}

static bool try_parse_payload(bytes_t raw, const ssz_def_t* def, uint32_t expected_fixed, ssz_ob_t* out) {
  if (!payload_fixed_len_is(raw, expected_fixed)) return false;
  c4_state_t tmp = {0};
  ssz_ob_t   ep  = {.def = def, .bytes = raw};
  bool       ok  = ssz_is_valid(ep, true, &tmp);
  c4_state_free(&tmp);
  if (ok) *out = ep;
  return ok;
}

c4_status_t op_el_from_preconf_bytes(c4_state_t* state, bytes_t preconf,
                                     bytes_t* el_header_out, ssz_ob_t* el_body_out, bytes32_t block_hash_out) {
  if (preconf.len < 33) {
    if (state) c4_state_add_error(state, "preconf payload too short");
    return C4_ERROR;
  }

  static const fork_id_t forks[] = {C4_FORK_GLOAS, C4_FORK_ELECTRA, C4_FORK_DENEB};
  static const struct {
    const ssz_def_t* def;
    uint32_t         fixed_len;
  } payloads[] = {
      {&ISTHMUS_EXECUTION_PAYLOAD_CONTAINER, OP_EP_ISTHMUS_FIXED},
      {&DENEB_EXECUTION_PAYLOAD_CONTAINER, OP_EP_DENEB_FIXED},
  };
  bytes_t   header  = NULL_BYTES;
  bytes32_t hash    = {0};
  ssz_ob_t  ep      = {0};
  bool      matched = false;

  for (size_t li = 0; li < sizeof(OP_PRECONF_LAYOUTS) / sizeof(OP_PRECONF_LAYOUTS[0]) && !matched; li++) {
    const op_preconf_layout_t* layout = &OP_PRECONF_LAYOUTS[li];
    if (preconf.len <= layout->prefix_len) continue;
    bytes_t payload = bytes_slice(preconf, layout->prefix_len, preconf.len - layout->prefix_len);

    for (size_t pi = 0; pi < sizeof(payloads) / sizeof(payloads[0]) && !matched; pi++) {
      if (!try_parse_payload(payload, payloads[pi].def, payloads[pi].fixed_len, &ep)) continue;

      bytes_t expected_hash = ssz_get(&ep, "blockHash").bytes;
      if (expected_hash.len != 32) continue;

      eth_el_header_ctx_t ectx = {0};
      ectx.execution_payload   = ep;
      ectx.state               = state;
      memcpy(ectx.parent_root, preconf.data, 32);
      ectx.has_requests_hash   = true;
      if (layout->has_requests_hash)
        memcpy(ectx.requests_hash, preconf.data + 32, 32);
      else
        op_empty_requests_hash(ectx.requests_hash);
      if (layout->has_slot) {
        ectx.has_slot = true;
        ectx.slot     = uint64_from_le(preconf.data + 64);
      }

      for (size_t i = 0; i < sizeof(forks) / sizeof(forks[0]); i++) {
        safe_free(header.data);
        header    = NULL_BYTES;
        ectx.fork = forks[i];
        if (eth_el_header_build_from_ep(&header, &ectx) != C4_SUCCESS || !header.data) continue;
        keccak(header, hash);
        if (memcmp(hash, expected_hash.data, 32) == 0) {
          matched = true;
          break;
        }
      }
    }
  }

  if (!matched) {
    safe_free(header.data);
    if (state) c4_state_add_error(state, "execution payload blockHash does not match keccak(RLP header)");
    return C4_ERROR;
  }

  ssz_builder_t body_builder = ssz_builder_for_type(ETH_SSZ_EL_BLOCK_CONTENT);
  ssz_add_bytes(&body_builder, "transactions", ssz_get(&ep, "transactions").bytes);
  ssz_add_bytes(&body_builder, "withdrawals", ssz_get(&ep, "withdrawals").bytes);
  *el_body_out   = ssz_builder_to_bytes(&body_builder);
  *el_header_out = header;
  memcpy(block_hash_out, hash, 32);
  return C4_SUCCESS;
}

static void adopt_header_snapshot(verify_ctx_t* ctx, bytes32_t block_hash, bytes_t header, bytes_t* el_header) {
  data_request_t* existing = c4_state_get_data_request_by_id(&ctx->state, block_hash);
  if (existing && existing->type == C4_DATA_TYPE_CACHE && existing->response.data) {
    safe_free(header.data);
    *el_header = existing->response;
    return;
  }
  data_request_t* snap = safe_calloc(1, sizeof(data_request_t));
  snap->type           = C4_DATA_TYPE_CACHE;
  snap->chain_id       = ctx->chain_id;
  snap->encoding       = C4_DATA_ENCODING_SSZ;
  snap->response       = header;
  snap->validated      = true;
  memcpy(snap->id, block_hash, 32);
  c4_state_add_request(&ctx->state, snap);
  *el_header = header;
}

c4_status_t op_verify_sequencer_proof(verify_ctx_t* ctx, ssz_ob_t block, bytes_t* el_header, bytes32_t block_hash) {
  if (!block.def || strcmp(block.def->name, "sequencerProof") != 0)
    THROW_ERROR("invalid block type!");

  const op_chain_config_t* config = op_get_chain_config(ctx->chain_id);
  if (!config) THROW_ERROR("chain not supported");

  ssz_ob_t payload_union = ssz_get(&block, "payload");
  ssz_ob_t signature     = ssz_get(&block, "signature");
  if (!payload_union.def || signature.bytes.len != 65) THROW_ERROR("invalid sequencer proof!");

  bool    compressed = payload_union.def->name && strcmp(payload_union.def->name, "uncompressed") != 0;
  bytes_t raw        = payload_union.bytes;
  bool    owned      = false;
  if (compressed) {
    TRY_ASYNC(op_decompress_preconf(&ctx->state, payload_union.bytes, &raw));
    owned = true;
  }
  else if (raw.len > OP_ZSTD_MAX_SIZE) {
    THROW_ERROR("preconf payload too large");
  }

  address_t signer = {0};
  if (!recover_sequencer(raw, signature.bytes, ctx->chain_id, signer) ||
      memcmp(config->sequencer_address, signer, 20) != 0) {
    if (owned) safe_free(raw.data);
    THROW_ERROR("invalid sequencer signature");
  }

  bytes_t     header = NULL_BYTES;
  ssz_ob_t    body   = {0};
  c4_status_t st     = op_el_from_preconf_bytes(&ctx->state, raw, &header, &body, block_hash);
  if (owned) safe_free(raw.data);
  if (st != C4_SUCCESS) {
    safe_free(header.data);
    safe_free(body.bytes.data);
    return C4_ERROR;
  }

#ifdef EL_HEADER_CACHE
  uint64_t block_number = eth_el_header_get_uint64(header, EL_BLOCK_NUMBER);
  if (block_number) c4_header_cache_put(ctx->chain_id, block_number, block_hash, header, &body);
#endif
  safe_free(body.bytes.data);
  adopt_header_snapshot(ctx, block_hash, header, el_header);
  return C4_SUCCESS;
}

void op_register_block_proof_verify(void) {
  c4_register_block_proof_verify(C4_CHAIN_TYPE_OP, op_verify_sequencer_proof);
}
