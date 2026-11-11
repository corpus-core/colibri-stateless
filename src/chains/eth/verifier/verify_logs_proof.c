/*
 * Copyright (c) 2025,2026 corpus.core GmbH
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
#include "eth_bloom.h"
#include "eth_tx.h"
#include "eth_verify.h"
#include "json.h"
#include "patricia.h"
#include "rlp.h"
#include "ssz.h"
#include "sync_committee.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GINDEX_TX_IN_LOGS_LIST_BASE 2097152L

static bool verify_tx(verify_ctx_t* ctx, ssz_ob_t block, ssz_ob_t tx, mpt_proof_t* receipt_proof, mpt_proof_t* tx_proof, uint64_t block_number, bytes32_t block_hash) {
  bytes_t   raw_receipt = {0};
  bytes_t   raw_tx      = {0};
  uint32_t  log_len     = ssz_len(ctx->data);
  uint32_t  tidx        = ssz_get_uint32(&tx, "transactionIndex");
  bytes32_t path_buf    = {0};
  buffer_t  path_buffer = stack_buffer(path_buf);
  bytes_t   path        = c4_eth_create_tx_path(tidx, &path_buffer);

  if (patricia_verify_multi(receipt_proof, path, &raw_receipt) != PATRICIA_FOUND) RETURN_VERIFY_ERROR(ctx, "invalid receipt proof!");
  if (patricia_verify_multi(tx_proof, path, &raw_tx) != PATRICIA_FOUND) RETURN_VERIFY_ERROR(ctx, "invalid tx proof!");

  for (int i = 0; i < log_len; i++) {
    ssz_ob_t log = ssz_at(ctx->data, i);
    if (block_number == ssz_get_uint64(&log, "blockNumber") && tidx == ssz_get_uint32(&log, "transactionIndex")) {
      if (!c4_tx_verify_log_data(ctx, log, block_hash, block_number, tidx, raw_tx, raw_receipt)) RETURN_VERIFY_ERROR(ctx, "invalid log data!");
    }
  }
  return true;
}

static c4_status_t verify_block(verify_ctx_t* ctx, ssz_ob_t block) {

  mpt_proof_t receipt_proof;
  mpt_proof_t tx_proof;
  ssz_ob_t    block_proof  = ssz_get(&block, "elProof");
  ssz_ob_t    txs          = ssz_get(&block, "txs");
  uint64_t    block_number = 0;
  uint32_t    tx_count     = ssz_len(txs);
  bytes_t     el_header    = {0};
  bytes32_t   block_hash   = {0};
  bool        success      = true;

  TRY_ASYNC(c4_verify_block(ctx, block_proof, &el_header, block_hash));
  block_number = eth_el_header_get_uint64(el_header, EL_BLOCK_NUMBER);
  if (block_number != ssz_get_uint64(&block, "blockNumber")) THROW_ERROR("invalid block number!");

  mpt_proof_init(&receipt_proof, ssz_get(&block, "receiptProof"), eth_el_header_get(el_header, EL_RECEIPTS_ROOT).data);
  mpt_proof_init(&tx_proof, ssz_get(&block, "transactionProof"), eth_el_header_get(el_header, EL_TRANSACTIONS_ROOT).data);

  // verify each tx and get the receipt root
  for (int i = 0; i < tx_count; i++) {
    if (!verify_tx(ctx, block, ssz_at(txs, i), &receipt_proof, &tx_proof, block_number, block_hash)) {
      success = false;
      break;
    }
  }

  mpt_proof_free(&receipt_proof);
  mpt_proof_free(&tx_proof);
  return success ? C4_SUCCESS : C4_ERROR;
}

static bool has_proof(verify_ctx_t* ctx, bytes_t block_number, bytes_t tx_index, uint32_t block_count) {
  for (int i = 0; i < block_count; i++) {
    ssz_ob_t block    = ssz_at(ctx->proof, i);
    bytes_t  block_bn = ssz_get(&block, "blockNumber").bytes;
    if (bytes_eq(block_number, block_bn)) {
      ssz_ob_t txs      = ssz_get(&block, "txs");
      uint32_t tx_count = ssz_len(txs);
      for (int j = 0; j < tx_count; j++) {
        ssz_ob_t tx = ssz_at(txs, j);
        if (bytes_eq(tx_index, ssz_get(&tx, "transactionIndex").bytes))
          return true;
      }
      return false;
    }
  }
  return false;
}

// Shape of an `eth_getLogs` filter; the same shape the prover accepts as `JSON_GET_LOGS_FILTER_FIELDS`.
#define LOGS_FILTER_SCHEMA "{fromBlock?:block,toBlock?:block,blockHash?:bytes32,topics?:[bytes32|[bytes32|null]|null],bloomFilter?:[bytes],address?:address|[address]}"

// quotes + "0x" + 16 nibbles: the longest JSON string holding a 64 bit block number
#define MAX_BLOCK_NUMBER_JSON_LEN 20

/**
 * Compares a block identifier with a block tag.
 *
 * @param block block identifier of a filter
 * @param tag the tag including its JSON quotes, e.g. `"\"safe\""`
 * @return `true` if the identifier is exactly this tag
 */
static bool is_block_tag(json_t block, const char* tag) {
  size_t len = strlen(tag);
  return block.type == JSON_TYPE_STRING && block.len == len && strncmp(block.start, tag, len) == 0;
}

/**
 * Reads one side of the block range of a filter.
 *
 * A block number bounds the range and `earliest` is block 0. `latest`, `safe` and
 * `finalized` leave the side open, because the verifier does not know the number such a
 * tag stands for; a missing value is treated the same way. Everything else is rejected:
 * a bound which can not be read must not be ignored.
 *
 * @param ctx verification context (error sink)
 * @param block `fromBlock` or `toBlock` of the filter, already validated as block
 * @param bounded receives whether `number` bounds the range
 * @param number receives the block number
 * @return `true` if the value is a supported block identifier
 */
static bool filter_block_bound(verify_ctx_t* ctx, json_t block, bool* bounded, uint64_t* number) {
  *bounded = false;
  *number  = 0;
  if (block.type == JSON_TYPE_NOT_FOUND) return true;
  if (is_block_tag(block, "\"earliest\"")) {
    *bounded = true;
    return true;
  }
  if (eth_json_is_latest(block) || is_block_tag(block, "\"safe\"") || is_block_tag(block, "\"finalized\"")) return true;
  if (block.type != JSON_TYPE_STRING || block.len < 5 || block.len > MAX_BLOCK_NUMBER_JSON_LEN || block.start[1] != '0' || block.start[2] != 'x')
    RETURN_VERIFY_ERROR(ctx, "unsupported block identifier in the filter");
  *bounded = true;
  *number  = json_as_uint64(block);
  return true;
}

/**
 * Checks that every log of the result lies in the blocks the filter asks for.
 *
 * A `blockHash` has to be the block of every log. Otherwise a numeric `fromBlock` and
 * `toBlock` bound the block number; a tag or a missing value leaves that side open,
 * because the verifier does not know the number a tag stands for.
 *
 * @param ctx verification context holding the verified logs in `data` (error sink)
 * @param filter filter object of the `eth_getLogs` request
 * @return `true` if no log lies outside the requested blocks
 */
static bool check_logs_block_range(verify_ctx_t* ctx, json_t filter) {
  json_t    block_hash = json_get(filter, "blockHash");
  bytes32_t hash       = {0};
  buffer_t  buf        = stack_buffer(hash);
  bool      by_hash    = block_hash.type != JSON_TYPE_NOT_FOUND;
  bool      has_from   = false;
  bool      has_to     = false;
  uint64_t  from       = 0;
  uint64_t  to         = 0;
  uint32_t  count      = ssz_len(ctx->data);

  if (by_hash && (block_hash.type != JSON_TYPE_STRING || block_hash.len != 68 || json_as_bytes(block_hash, &buf).len != 32))
    RETURN_VERIFY_ERROR(ctx, "invalid blockHash in the filter");
  if (!by_hash && !filter_block_bound(ctx, json_get(filter, "fromBlock"), &has_from, &from)) return false;
  if (!by_hash && !filter_block_bound(ctx, json_get(filter, "toBlock"), &has_to, &to)) return false;

  for (uint32_t i = 0; i < count; i++) {
    ssz_ob_t log    = ssz_at(ctx->data, i);
    uint64_t number = ssz_get_uint64(&log, "blockNumber");
    if (by_hash) {
      bytes_t log_hash = ssz_get(&log, "blockHash").bytes;
      if (log_hash.len != 32 || memcmp(log_hash.data, hash, 32) != 0) RETURN_VERIFY_ERROR(ctx, "log is not part of the requested block");
      continue;
    }
    if (has_from && number < from) RETURN_VERIFY_ERROR(ctx, "log is older than the requested block range");
    if (has_to && number > to) RETURN_VERIFY_ERROR(ctx, "log is newer than the requested block range");
  }
  return true;
}

/**
 * Checks the logs of an `eth_getLogs` result against the filter of the request.
 *
 * The proof only shows that each log is part of a receipt. Whether it is a log the
 * request asked for is decided here: address and topics have to match the filter and the
 * block has to lie in the requested range. In PAP mode the prover does not know address
 * and topics and returns more logs than requested, so the logs which do not match are
 * dropped instead of rejected.
 *
 * The filter is validated first. A filter entry which can not be read would otherwise be
 * skipped and match every log.
 *
 * @param ctx verification context holding the verified logs in `data` (error sink)
 * @return `true` if the logs left in `data` are logs the filter asks for
 */
static bool apply_logs_filter(verify_ctx_t* ctx) {
  json_t filter = json_at(ctx->args, 0);
  if (filter.type != JSON_TYPE_OBJECT) RETURN_VERIFY_ERROR(ctx, "eth_getLogs requires a filter object in args");
  CHECK_JSON_VERIFY(filter, LOGS_FILTER_SCHEMA, "Invalid eth_getLogs filter: ");
  if (json_len(json_get(filter, "topics")) > C4_ETH_LOG_MAX_TOPICS) RETURN_VERIFY_ERROR(ctx, "Invalid eth_getLogs filter: a log has at most 4 topics");

  ssz_ob_t filtered = c4_eth_filter_logs(ctx->data, filter);
#ifdef PAP
  if (ctx->flags & VERIFY_FLAG_PAP) {
    if (ctx->flags & VERIFY_FLAG_FREE_DATA)
      safe_free(ctx->data.bytes.data);
    ctx->data = filtered;
    ctx->flags |= VERIFY_FLAG_FREE_DATA;
    return check_logs_block_range(ctx, filter);
  }
#endif
  bool all_match = ssz_len(filtered) == ssz_len(ctx->data);
  safe_free(filtered.bytes.data);
  if (!all_match) RETURN_VERIFY_ERROR(ctx, "log does not match the filter of the request");
  return check_logs_block_range(ctx, filter);
}

/**
 * Takes the logs to verify from the arguments of an `eth_verifyLogs` request.
 *
 * `eth_verifyLogs` verifies the logs of the request and never logs chosen by the prover,
 * so a request carrying data is rejected. `VERIFY_FLAG_FREE_DATA` is never accepted from
 * the host: if it is set, the data was built here by an earlier run of the same
 * verification which had to wait for a pending request.
 *
 * @param ctx verification context holding the logs as arguments (error sink)
 * @return `true` if `data` holds the logs of the request
 */
static bool load_logs_from_args(verify_ctx_t* ctx) {
  if (ctx->flags & VERIFY_FLAG_FREE_DATA) return true;
  if (ctx->data.def->type != SSZ_TYPE_NONE) RETURN_VERIFY_ERROR(ctx, "data must be empty; eth_verifyLogs verifies the logs of the request!");
  if (ctx->args.type != JSON_TYPE_ARRAY) RETURN_VERIFY_ERROR(ctx, "eth_verifyLogs requires the logs in args");
  ctx->data = ssz_from_json(ctx->args, eth_ssz_verification_type(ETH_SSZ_DATA_LOGS), &ctx->state);
  ctx->flags |= VERIFY_FLAG_FREE_DATA;
  return ctx->state.error == NULL;
}

bool verify_logs_proof(verify_ctx_t* ctx) {
  bool is_get_logs    = ctx->method && strcmp(ctx->method, "eth_getLogs") == 0;
  bool is_verify_logs = ctx->method && strcmp(ctx->method, "eth_verifyLogs") == 0;
  if (!is_get_logs && !is_verify_logs) RETURN_VERIFY_ERROR(ctx, "method mismatch for logs proof");

  // in case we just proof the tx, we copy the arg as data, so data ia always set.
  // The logs are taken from the arguments before anything is counted or compared, so a
  // proof which covers none of them is detected as well.
  if (is_verify_logs && !load_logs_from_args(ctx)) return false;
  // Every check below walks the list of logs, so any other data would pass unchecked.
  if (!ssz_is_type(&ctx->data, eth_ssz_verification_type(ETH_SSZ_DATA_LOGS))) RETURN_VERIFY_ERROR(ctx, "logs proof requires a list of logs as data");

  uint32_t log_count   = ssz_len(ctx->data);
  uint32_t block_count = ssz_len(ctx->proof);

  for (int i = 0; i < block_count; i++) {
    if (verify_block(ctx, ssz_at(ctx->proof, i)) != C4_SUCCESS) return false;
  }

  for (int i = 0; i < log_count; i++) {
    ssz_ob_t log = ssz_at(ctx->data, i);
    if (!has_proof(ctx, ssz_get(&log, "blockNumber").bytes, ssz_get(&log, "transactionIndex").bytes, block_count)) RETURN_VERIFY_ERROR(ctx, "missing log proof!");
  }

  if (is_get_logs && !apply_logs_filter(ctx)) return false;

  ctx->success = true;
  return true;
}
