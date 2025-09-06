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

static bool is_unsafe_signer(chain_id_t chain_id, address_t address) {
  const op_chain_config_t* config = op_get_chain_config(chain_id);
  if (config == NULL) {
    return false; // Chain not supported
  }
  return memcmp(config->sequencer_address, address, 20) == 0;
}

bool op_verify_block_proof(verify_ctx_t* ctx) {
  address_t signer          = {0};
  json_t    block_number    = json_at(ctx->args, 0);
  bool      include_txs     = json_as_bool(json_at(ctx->args, 1));
  ssz_ob_t  block_proof     = ssz_get(&ctx->proof, "block_proof");
  ssz_ob_t  compressed_data = ssz_get(&block_proof, "payload");
  ssz_ob_t  signature       = ssz_get(&block_proof, "signature");

  size_t expected_size = op_zstd_get_decompressed_size(compressed_data.bytes);
  if (expected_size == 0) RETURN_VERIFY_ERROR(ctx, "failed to get decompressed size");

  bytes_t decompressed_data = bytes(safe_malloc(expected_size), expected_size);
  size_t  actual_size       = op_zstd_decompress(compressed_data.bytes, decompressed_data);

  // Verify signature from sequencer
  verify_signature(decompressed_data, signature.bytes, ctx->chain_id, signer);

  if (!is_unsafe_signer(ctx->chain_id, signer)) {
    safe_free(decompressed_data.data);
    RETURN_VERIFY_ERROR(ctx, "invalid sequencer signature");
  }

  ssz_def_t payload_def     = SSZ_CONTAINER("payload", DENEP_EXECUTION_PAYLOAD);
  ssz_ob_t  exec_payload    = (ssz_ob_t) {.def = &payload_def, .bytes = bytes_slice(decompressed_data, 32, decompressed_data.len - 32)};
  bytes32_t parent_root     = {0};
  bytes32_t withdrawel_root = {0};

  eth_set_block_data(ctx, exec_payload, parent_root, withdrawel_root, include_txs);
  safe_free(decompressed_data.data);
  ctx->success = true;
  return true;
}
