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
#include "chains.h"
#include "crypto.h"
#include "eth_account.h"
#include "eth_tx.h"
#include "eth_verify.h"
#include "json.h"
#include "logger.h"
#include "op_chains_conf.h"
#include "op_types.h"
#include "op_verify.h"
#include "op_zstd.h"
#include "patricia.h"
#include "rlp.h"
#include "ssz.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const ssz_def_t EXECUTION_PAYLOAD_CONTAINER = SSZ_CONTAINER("payload", DENEP_EXECUTION_PAYLOAD);

static void verify_signature(bytes_t data, bytes_t signature, uint64_t chain_id, address_t address) {
  uint8_t buf[96] = {0};
  uint8_t pub[64] = {0};
  uint64_to_be(buf + 64 - 8, chain_id);
  keccak(data, buf + 64);
  keccak(bytes(buf, 96), buf);
  secp256k1_recover(buf, signature, pub);
  keccak(bytes(pub, 64), buf);
  memcpy(address, buf + 12, 20);
}

ssz_ob_t* op_extract_verified_execution_payload(verify_ctx_t* ctx, ssz_ob_t block_proof, json_t block_number) {
  const op_chain_config_t* config          = op_get_chain_config(ctx->chain_id);
  address_t                signer          = {0};
  ssz_ob_t                 compressed_data = ssz_get(&block_proof, "payload");
  ssz_ob_t                 signature       = ssz_get(&block_proof, "signature");

  if (config == NULL) {
    c4_state_add_error(&ctx->state, "chain not supported");
    return NULL;
  }

  size_t expected_size = op_zstd_get_decompressed_size(compressed_data.bytes);
  if (expected_size == 0) RETURN_VERIFY_ERROR(ctx, "failed to get decompressed size");

  bytes_t decompressed_data = bytes(safe_malloc(expected_size), expected_size);
  size_t  actual_size       = op_zstd_decompress(compressed_data.bytes, decompressed_data);

  // Verify signature from sequencer
  verify_signature(decompressed_data, signature.bytes, ctx->chain_id, signer);

  if (memcmp(config->sequencer_address, signer, 20)) {
    safe_free(decompressed_data.data);
    c4_state_add_error(&ctx->state, "invalid sequencer signature");
    return NULL;
  }

  // here we use the fact, that the execution payload starts at the 32nd byte
  // so we store the ssz_ob_t at the beginning of the decompressed data, so we can then easily use it to call free on it.
  ssz_ob_t* execution_payload = (void*) decompressed_data.data;
  execution_payload->def      = &EXECUTION_PAYLOAD_CONTAINER;
  execution_payload->bytes    = bytes_slice(decompressed_data, 32, decompressed_data.len - 32);

  // check blocknumber
  if (block_number.len > 2 && block_number.start[1] == '0' && block_number.start[2] == 'x') {
    bytes32_t buf    = {0};
    buffer_t  buffer = stack_buffer(buf);
    if (block_number.len == 68) { // must be blockhash
      json_as_bytes(block_number, &buffer);
      bytes_t block_hash = ssz_get(execution_payload, "blockHash").bytes;
      if (memcmp(buf, block_hash.data, 32)) {
        safe_free(execution_payload);
        c4_state_add_error(&ctx->state, "blockhash mismatch");
        return NULL;
      }
    }
    else if (json_as_uint64(block_number) != ssz_get_uint64(execution_payload, "blockNumber")) {
      safe_free(execution_payload);
      c4_state_add_error(&ctx->state, "blocknumber mismatch");
      return NULL;
    }
  }

  return execution_payload;
}

bool op_verify_block(verify_ctx_t* ctx) {
  json_t   block_number = json_at(ctx->args, 0);
  bool     include_txs  = json_as_bool(json_at(ctx->args, 1));
  ssz_ob_t block_proof  = ssz_get(&ctx->proof, "block_proof");

  ssz_ob_t* execution_payload = op_extract_verified_execution_payload(ctx, block_proof, block_number);
  if (!execution_payload) return false;

  bytes32_t parent_root     = {0};
  bytes32_t withdrawel_root = {0};

  log_info("include_txs: %d\n", include_txs);

  eth_set_block_data(ctx, *execution_payload, parent_root, withdrawel_root, include_txs);
  safe_free(execution_payload);
  ctx->success = true;
  return true;
}
