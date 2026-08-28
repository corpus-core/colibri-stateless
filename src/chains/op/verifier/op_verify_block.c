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
#include <stdbool.h>
#include <string.h>

#define OP_ZSTD_MAX_SIZE (32u * 1024u * 1024u)

static const ssz_def_t EXECUTION_PAYLOAD_CONTAINER = SSZ_CONTAINER("payload", DENEP_EXECUTION_PAYLOAD);

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
  bytes_t buf        = bytes(safe_malloc(expected), (uint32_t) expected);
  size_t  actual     = op_zstd_decompress(compressed, buf);
  if (actual != expected) {
    safe_free(buf.data);
    if (state) c4_state_add_error(state, "failed to decompress data");
    return C4_ERROR;
  }
  *out = buf;
  return C4_SUCCESS;
}

c4_status_t op_el_from_preconf_bytes(c4_state_t* state, bytes_t preconf,
                                     bytes_t* el_header_out, ssz_ob_t* el_body_out, bytes32_t block_hash_out) {
  if (preconf.len < 33) {
    if (state) c4_state_add_error(state, "preconf payload too short");
    return C4_ERROR;
  }

  ssz_ob_t ep = {
      .def   = &EXECUTION_PAYLOAD_CONTAINER,
      .bytes = bytes_slice(preconf, 32, preconf.len - 32),
  };
  if (!ssz_is_valid(ep, true, state)) {
    if (state && !state->error) c4_state_add_error(state, "invalid execution payload");
    return C4_ERROR;
  }

  bytes_t expected_hash = ssz_get(&ep, "blockHash").bytes;
  if (expected_hash.len != 32) {
    if (state) c4_state_add_error(state, "execution payload has no blockHash field");
    return C4_ERROR;
  }

  eth_el_header_ctx_t ectx = {0};
  ectx.execution_payload   = ep;
  ectx.state               = state;
  memcpy(ectx.parent_root, preconf.data, 32);

  static const fork_id_t forks[] = {C4_FORK_DENEB, C4_FORK_ELECTRA, C4_FORK_GLOAS};
  bytes_t                header  = NULL_BYTES;
  bytes32_t              hash    = {0};
  bool                   matched = false;
  for (size_t i = 0; i < sizeof(forks) / sizeof(forks[0]); i++) {
    safe_free(header.data);
    header     = NULL_BYTES;
    ectx.fork  = forks[i];
    if (eth_el_header_build_from_ep(&header, &ectx) != C4_SUCCESS || !header.data) continue;
    keccak(header, hash);
    if (memcmp(hash, expected_hash.data, 32) == 0) {
      matched = true;
      break;
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

  bytes_t  header = NULL_BYTES;
  ssz_ob_t body   = {0};
  c4_status_t st  = op_el_from_preconf_bytes(&ctx->state, raw, &header, &body, block_hash);
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
