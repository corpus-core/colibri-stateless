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

#include "btc_header.h"
#include "btc_hash.h"
#include "btc_merkle.h"
#include "btc_tx.h"
#include "btc_types.h"
#include <string.h>

/**
 * Returns the `BtcHeader` data variant.
 *
 * @return the 80-byte header definition stored in the request data union
 */
static const ssz_def_t* header_data_def(void) {
  const ssz_def_t* request = btc_ssz_verification_type(BTC_SSZ_VERIFY_REQUEST);
  return &request->def.container.elements[1].def.container.elements[1];
}

/**
 * Checks that the coinbase transaction commits `height` in the header merkle root.
 *
 * Heights below `BTC_BIP34_START` are not bound by the script. The caller rejects them before a
 * target is cached.
 *
 * @param raw coinbase transaction bytes
 * @param tx_count number of transactions in the block
 * @param merkle concatenated sibling hashes
 * @param root header merkle root in internal order
 * @param height claimed block height
 * @return true when the coinbase is the first transaction and its BIP34 height matches
 */
static bool header_coinbase(bytes_t raw, uint32_t tx_count, bytes_t merkle, const uint8_t root[32], uint32_t height) {
  btc_tx_t tx;
  uint8_t  id[32];
  uint8_t  zeros[32] = {0};
  bytes_t  prev;
  bytes_t  script;
  uint32_t prev_index = 0;
  uint32_t encoded    = 0;
  if (tx_count == 0 || !raw.data || raw.len == 0) return false;
  if (!btc_tx_parse(raw, &tx) || tx.input_count == 0) return false;
  if (!btc_tx_id(&tx, id)) return false;
  if (!btc_tx_input(&tx, 0, &prev, &prev_index, &script, NULL)) return false;
  if (prev.len != 32 || memcmp(prev.data, zeros, 32) != 0 || prev_index != 0xffffffffu) return false;
  if (!btc_merkle_verify(id, 0, tx_count, merkle, root)) return false;
  if (height < BTC_BIP34_START) return true;
  return btc_bip34_height(script, &encoded) && encoded == height;
}

bool btc_verify_header_proof(verify_ctx_t* ctx, btc_target_cache_t* cache) {
  btc_target_cache_t working;
  ssz_ob_t           header;
  ssz_ob_t           finals;
  ssz_ob_t           merkle;
  ssz_ob_t           coinbase;
  uint8_t            hash[32];
  uint8_t            display[32];
  uint8_t            checkpoint[32];
  uint8_t            siblings[32 * 32];
  uint32_t           height;
  uint32_t           tx_count;
  uint32_t           n;
  uint32_t           i;
  bytes_t            want;
  uint8_t            want_buf[40];
  buffer_t           want_out = stack_buffer(want_buf);
  if (!ctx) return false;
  if (!cache || !ctx->proof.def || !ctx->proof.def->name || strcmp(ctx->proof.def->name, "BtcHeaderProof") != 0)
    RETURN_VERIFY_ERROR(ctx, "proof is not a header proof");
  if (ctx->chain_id != C4_CHAIN_BTC_MAINNET) RETURN_VERIFY_ERROR(ctx, "bitcoin network is not supported");
  if (!ctx->method || strcmp(ctx->method, "getblockheader") != 0) RETURN_VERIFY_ERROR(ctx, "method mismatch for header proof");
  if (!btc_header_raw_requested(ctx->args)) RETURN_VERIFY_ERROR(ctx, "raw block header was not requested");
  if (!ssz_is_valid(ctx->proof, true, &ctx->state)) RETURN_VERIFY_ERROR(ctx, "invalid header proof");

  header   = ssz_get(&ctx->proof, "header");
  finals   = ssz_get(&ctx->proof, "final_headers");
  merkle   = ssz_get(&ctx->proof, "merkle");
  coinbase = ssz_get(&ctx->proof, "coinbase");
  height   = ssz_get_uint32(&ctx->proof, "height");
  tx_count = ssz_get_uint32(&ctx->proof, "tx_count");
  if (!header.bytes.data || header.bytes.len != BTC_HEADER_SIZE) RETURN_VERIFY_ERROR(ctx, "invalid block header");
  if (height < BTC_BIP34_START) RETURN_VERIFY_ERROR(ctx, "block height is below BIP34");
  n = ssz_len(finals);
  if (n < BTC_FINALITY || height > UINT32_MAX - n) RETURN_VERIFY_ERROR(ctx, "block is not final");
  if (ssz_len(merkle) > 32) RETURN_VERIFY_ERROR(ctx, "merkle proof is too deep");

  working = *cache;
  if (!btc_header_check(header.bytes, hash)) RETURN_VERIFY_ERROR(ctx, "header proof-of-work failed");
  if (!btc_target_accept_header(&working, NULL, height, header.bytes)) RETURN_VERIFY_ERROR(ctx, "header target is not authorized");
  for (i = 0; i < n; i++) {
    ssz_ob_t item = ssz_at(finals, i);
    if (!item.bytes.data || item.bytes.len != BTC_HEADER_SIZE) RETURN_VERIFY_ERROR(ctx, "invalid finality header");
    if (memcmp(item.bytes.data + BTC_HEADER_PARENT, hash, 32) != 0) RETURN_VERIFY_ERROR(ctx, "finality headers do not link");
    if (!btc_header_check(item.bytes, hash)) RETURN_VERIFY_ERROR(ctx, "finality header proof-of-work failed");
    if (!btc_target_accept_header(&working, NULL, height + 1u + i, item.bytes)) RETURN_VERIFY_ERROR(ctx, "finality target is not authorized");
  }

  btc_header_check(header.bytes, hash);
  btc_header_check(btc_mainnet_checkpoint_header(), checkpoint);
  if (coinbase.bytes.len == 0 && ssz_len(merkle) == 0 && tx_count == 0) {
    if (height != BTC_MAINNET_CHECKPOINT_HEIGHT || memcmp(hash, checkpoint, 32) != 0)
      RETURN_VERIFY_ERROR(ctx, "missing coinbase");
  }
  else {
    for (i = 0; i < ssz_len(merkle); i++) {
      ssz_ob_t sibling = ssz_at(merkle, i);
      if (!sibling.bytes.data || sibling.bytes.len != 32) RETURN_VERIFY_ERROR(ctx, "invalid merkle sibling");
      memcpy(siblings + (size_t) i * 32, sibling.bytes.data, 32);
    }
    if (!header_coinbase(coinbase.bytes, tx_count, bytes(siblings, ssz_len(merkle) * 32), header.bytes.data + BTC_HEADER_MERKLE, height))
      RETURN_VERIFY_ERROR(ctx, "coinbase does not commit the block height");
  }

  btc_hash_reverse(hash, display);
  want = json_as_bytes(json_at(ctx->args, 0), &want_out);
  if (want.len != 32 || memcmp(want.data, display, 32) != 0) RETURN_VERIFY_ERROR(ctx, "block hash mismatch");
  if (ctx->data.def && ctx->data.def->type != SSZ_TYPE_NONE) {
    if (!ctx->data.bytes.data || ctx->data.bytes.len != BTC_HEADER_SIZE || memcmp(ctx->data.bytes.data, header.bytes.data, BTC_HEADER_SIZE) != 0)
      RETURN_VERIFY_ERROR(ctx, "header data does not match the proof");
  }
  else {
    uint8_t* copy   = safe_malloc(BTC_HEADER_SIZE);
    ctx->data.def   = header_data_def();
    ctx->data.bytes = bytes(copy, BTC_HEADER_SIZE);
    memcpy(copy, header.bytes.data, BTC_HEADER_SIZE);
    ctx->flags |= VERIFY_FLAG_FREE_DATA;
  }
  *cache       = working;
  ctx->success = true;
  return true;
}
