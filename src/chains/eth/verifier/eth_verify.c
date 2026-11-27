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

#include "eth_verify.h"
#include "beacon_types.h"
#include "chains.h"
#include "el_header.h"
#include "eth_bloom.h"
#include "header_cache.h"
#include "json.h"
#ifdef PAP
#include "pap_tx_cache.h"
#endif
#include "ssz.h"
#include "sync_committee.h"
#include "verify.h"
#include <stdint.h>
#include <string.h>

#ifdef ETH_OBLIVIOUS
#include "retry_delay.h"
#endif

// Freshness gate for `"latest"` proofs (shared by all block-tag methods)

bool eth_json_is_latest(json_t block_tag) {
  static const size_t LATEST_LITERAL_LEN = sizeof("\"latest\"") - 1;
  return block_tag.type == JSON_TYPE_STRING &&
         block_tag.len == LATEST_LITERAL_LEN &&
         strncmp(block_tag.start, "\"latest\"", LATEST_LITERAL_LEN) == 0;
}

bool eth_check_latest_freshness(verify_ctx_t* ctx, bool is_latest, bool has_ts, uint64_t block_ts) {
  if (!ctx->min_latest_block_ts || !is_latest) return true;
  // EEZ milestone 1: `latest` is the last L2 block whose hash was posted to L1, which
  // is up to 4 hours old while the L2 is idle. The gate would reject every such proof,
  // so it is suspended for EEZ until newer blocks can be proven (milestone 2).
  // TODO(eez-milestone-2): remove this exception once L2 blocks newer than the L1 anchor are provable.
  if (c4_chain_type(ctx->chain_id) == C4_CHAIN_TYPE_EEZ) return true;
  if (!has_ts) RETURN_VERIFY_ERROR(ctx, "cannot verify freshness of latest block without block context");
  if (block_ts < ctx->min_latest_block_ts) RETURN_VERIFY_ERROR(ctx, "proof for latest too old");
  return true;
}

#ifdef ETH_OBLIVIOUS

uint32_t eth_oblivious_retry_delay(chain_id_t chain, uint16_t retry_count) {
  return c4_retry_delay_for(C4_RETRY_CATEGORY_OBLIVIOUS, chain, retry_count);
}

void eth_oblivious_retry_observe(chain_id_t chain, uint16_t retry_count) {
  c4_retry_delay_observe(C4_RETRY_CATEGORY_OBLIVIOUS, chain, retry_count);
}

bool eth_is_oblivious_unavailable(json_t response) {
  if (response.type != JSON_TYPE_OBJECT) return false;
  json_t error = json_get(response, "error");
  if (error.type != JSON_TYPE_OBJECT) return false;

  json_t code = json_get(error, "code");
  if (code.type != JSON_TYPE_NUMBER || code.len != 6 || strncmp(code.start, "-32001", 6) != 0) return false;

  json_t message = json_get(error, "message");
  if (message.type != JSON_TYPE_STRING) return false;
  // message.start/len spans the JSON string token (incl. quotes); a substring
  // search is sufficient to recognise the oblivious node's availability signal.
  static const char marker[]   = "data non availability";
  const uint32_t    marker_len = sizeof(marker) - 1;
  for (uint32_t i = 0; i + marker_len <= message.len; i++) {
    if (strncmp(message.start + i, marker, marker_len) == 0) return true;
  }
  return false;
}

#endif // ETH_OBLIVIOUS

// : Ethereum

// :: Supported RPC-Methods
//
// The following table shows the supported RPC-Methods for Ethereum.
// Execution-layer values are proven against a verified RLP EL header whose
// `keccak256` hash is bound to the beacon `bodyRoot` (`ETH_EL_PROOF_UNION`).
//

static const char* proofable_methods[] = {
    RPC_METHOD("eth_call", Bytes, EthCallProof),
    RPC_METHOD("eth_estimateGas", Uint64, EthCallProof),
    RPC_METHOD("colibri_simulateTransaction", EthSimulationResult, EthCallProof),
    RPC_METHOD("eth_getProof", EthProofData, EthAccountProof),
    RPC_METHOD("eth_getBalance", Uint256, EthAccountProof),
    RPC_METHOD("eth_getBlockByHash", EthBlockData, EthBlockProof),
    RPC_METHOD("eth_getBlockByNumber", EthBlockData, EthBlockProof),
    RPC_METHOD("eth_getCode", Bytes, EthAccountProof),
    RPC_METHOD("eth_getLogs", ListEthReceiptDataLog, ListEthLogsBlock), // - currently everthing except the logIndex is verified
    RPC_METHOD("eth_verifyLogs", Void, ListEthLogsBlock),
    RPC_METHOD("eth_getTransactionCount", Uint256, EthAccountProof),
    RPC_METHOD("eth_getStorageAt", Bytes32, EthAccountProof),
    RPC_METHOD("eth_getTransactionReceipt", EthReceiptData, EthReceiptProof),
    RPC_METHOD("eth_getBlockReceipts", ListEthBlockReceipts, BlockReceiptsProof),
    RPC_METHOD("eth_getTransactionByHash", EthTxData, EthTransactionProof),
    RPC_METHOD("eth_getTransactionByBlockHashAndIndex", EthTxData, EthTransactionProof),
    RPC_METHOD("eth_getTransactionByBlockNumberAndIndex", EthTxData, EthTransactionProof),
    RPC_METHOD("eth_blockNumber", Uint256, EthBlockProof),
    RPC_METHOD("eth_getBlockHeader", EthBlockHeaderData, EthBlockProof),
    RPC_METHOD("eth_blobBaseFee", Uint256, EthBlockProof),
    RPC_METHOD("eth_maxPriorityFeePerGas", Uint256, EthBlockProof),
    RPC_METHOD("eth_newPendingTransactionFilter", Void, Void),
    RPC_METHOD("eth_newFilter", Void, Void),
    RPC_METHOD("eth_newBlockFilter", Void, Void),
    RPC_METHOD("eth_getFilterChanges", Void, Void),
    RPC_METHOD("eth_getFilterLogs", Void, Void),
    RPC_METHOD("eth_uninstallFilter", Uint256, Void),
    RPC_METHOD("eth_subscribe", Uint256, Void),
    RPC_METHOD("eth_unsubscribe", Uint256, Void),
};

// Expected proof container (SSZ union variant) per entry of `proofable_methods`.
//
// Verification must bind the *carried* proof container to the proof type the
// requested method was proven for. Without this check, the dispatcher routed on the
// *attacker-controlled* union tag alone, so an attacker could answer e.g.
// `eth_getBalance` with an empty `eth_verifyLogs` proof container and have a
// well-formed-but-meaningless proof accepted. `proofable_methods` gates which methods
// require a proof at all; this table pins *which* proof each method requires.
//
// Index-aligned with `proofable_methods`. `0` (ETH_SSZ_DATA_NONE) means "no proof
// accepted" for rows whose third column is `Void` (those methods are already
// METHOD_UNPROOFABLE / local, so the row only hardens an unreachable path).
#define PROOF_ROW(data, proof) ETH_SSZ_##proof
#define PROOF_VOID           0
static const eth_ssz_type_t proofable_proof_types[] = {
    PROOF_ROW(Bytes, VERIFY_CALL_PROOF),                         // eth_call
    PROOF_ROW(Uint64, VERIFY_CALL_PROOF),                        // eth_estimateGas
    PROOF_ROW(EthSimulationResult, VERIFY_CALL_PROOF),           // colibri_simulateTransaction
    PROOF_ROW(EthProofData, VERIFY_ACCOUNT_PROOF),               // eth_getProof
    PROOF_ROW(Uint256, VERIFY_ACCOUNT_PROOF),                    // eth_getBalance
    PROOF_ROW(EthBlockData, VERIFY_BLOCK_PROOF),                 // eth_getBlockByHash
    PROOF_ROW(EthBlockData, VERIFY_BLOCK_PROOF),                 // eth_getBlockByNumber
    PROOF_ROW(Bytes, VERIFY_ACCOUNT_PROOF),                      // eth_getCode
    PROOF_ROW(ListEthReceiptDataLog, VERIFY_LOGS_PROOF),         // eth_getLogs
    PROOF_ROW(Void, VERIFY_LOGS_PROOF),                          // eth_verifyLogs
    PROOF_ROW(Uint256, VERIFY_ACCOUNT_PROOF),                    // eth_getTransactionCount
    PROOF_ROW(Bytes32, VERIFY_ACCOUNT_PROOF),                    // eth_getStorageAt
    PROOF_ROW(EthReceiptData, VERIFY_RECEIPT_PROOF),             // eth_getTransactionReceipt
    PROOF_ROW(ListEthBlockReceipts, VERIFY_BLOCK_RECEIPTS_PROOF), // eth_getBlockReceipts
    PROOF_ROW(EthTxData, VERIFY_TRANSACTION_PROOF),              // eth_getTransactionByHash
    PROOF_ROW(EthTxData, VERIFY_TRANSACTION_PROOF),              // eth_getTransactionByBlockHashAndIndex
    PROOF_ROW(EthTxData, VERIFY_TRANSACTION_PROOF),              // eth_getTransactionByBlockNumberAndIndex
    PROOF_ROW(Uint256, VERIFY_BLOCK_PROOF),                      // eth_blockNumber
    PROOF_ROW(EthBlockHeaderData, VERIFY_BLOCK_PROOF),           // eth_getBlockHeader
    PROOF_ROW(Uint256, VERIFY_BLOCK_PROOF),                      // eth_blobBaseFee
    PROOF_ROW(Uint256, VERIFY_BLOCK_PROOF),                      // eth_maxPriorityFeePerGas
    PROOF_VOID, // eth_newPendingTransactionFilter
    PROOF_VOID, // eth_newFilter
    PROOF_VOID, // eth_newBlockFilter
    PROOF_VOID, // eth_getFilterChanges
    PROOF_VOID, // eth_getFilterLogs
    PROOF_VOID, // eth_uninstallFilter
    PROOF_VOID, // eth_subscribe
    PROOF_VOID, // eth_unsubscribe
};
#undef PROOF_ROW
#undef PROOF_VOID

// Compile-time guard: the two tables must stay index-aligned.
typedef char proofable_proof_types_drift
    [(sizeof(proofable_proof_types) / sizeof(proofable_proof_types[0]) ==
      sizeof(proofable_methods) / sizeof(proofable_methods[0]))
         ? 1
         : -1];

// Returns the SSZ union variant the request *must* carry for `ctx->method`, or 0 when
// the method is not in the proofable table (local / unproofable / unknown).
static eth_ssz_type_t eth_expected_proof_type(const verify_ctx_t* ctx) {
  if (!ctx->method) return 0;
  for (size_t i = 0; i < sizeof(proofable_methods) / sizeof(proofable_methods[0]); i++) {
    if (strcmp(ctx->method, proofable_methods[i]) == 0)
      return proofable_proof_types[i];
  }
  return 0;
}
static const char* local_methods[] = {
    RPC_METHOD("eth_chainId", Uint64, Void),
    RPC_METHOD("eth_accounts", ListAddress, Void),
    RPC_METHOD("eth_protocolVersion", Uint256, Void),
    RPC_METHOD("web3_clientVersion", String, Void),
    RPC_METHOD("web3_sha3", Bytes32, Void),
    RPC_METHOD("net_version", String, Void),
    RPC_METHOD("colibri_decodeTransaction", EthTxData, Void),
};

static const char* not_verifieable_yet_methods[] = {
    RPC_METHOD("eth_getUncleByBlockHashAndIndex", Void, Void),
    RPC_METHOD("eth_getUncleByBlockNumberAndIndex", Void, Void),
    RPC_METHOD("eth_getBlockTransactionCountByHash", Void, Void),
    RPC_METHOD("eth_getBlockTransactionCountByNumber", Void, Void),
    RPC_METHOD("eth_feeHistory", Void, Void),
    RPC_METHOD("eth_createAccessList", EthAccessData, EthCallProof),
    RPC_METHOD("eth_gasPrice", Void, Void),
    RPC_METHOD("eth_getUncleByBlockHash", Void, Void),
    RPC_METHOD("eth_getUncleByBlockNumber", Void, Void),
    RPC_METHOD("eth_getUncleCountByBlockHash", Void, Void),
    RPC_METHOD("eth_getUncleCountByBlockNumber", Void, Void),
    RPC_METHOD("eth_sendRawTransaction", Void, Void),
};

static bool is_nullable_method(char* method) {
  return method && (strcmp(method, "eth_getTransactionByHash") == 0 || strcmp(method, "eth_getTransactionByBlockHashAndIndex") == 0 || strcmp(method, "eth_getTransactionByBlockNumberAndIndex") == 0 || strcmp(method, "eth_getTransactionReceipt") == 0);
}
static bool is_call_method(char* method) {
  return strcmp(method, "eth_call") == 0 || strcmp(method, "eth_estimateGas") == 0 || strcmp(method, "colibri_simulateTransaction") == 0;
}
#ifdef PAP
static bool is_pap_tx_method(char* method) {
  return strcmp(method, "eth_getTransactionReceipt") == 0 ||
         strcmp(method, "eth_getTransactionByHash") == 0 ||
         strcmp(method, "eth_getTransactionByBlockNumberAndIndex") == 0 ||
         strcmp(method, "eth_sendRawTransaction") == 0 ||
         strcmp(method, "eth_sendTransaction") == 0;
}
#endif

static bool no_proof(verify_ctx_t* ctx) {
  return ctx->proof.def->type == SSZ_TYPE_NONE;
}

// Returns true if `block_tag` is the JSON string `literal` (which must include the surrounding
// quotes, e.g. `"\"pending\""`). Matches the exact-length comparison used by `eth_json_is_latest`.
static bool eth_json_is_tag(json_t block_tag, const char* literal, size_t literal_len) {
  return block_tag.type == JSON_TYPE_STRING &&
         block_tag.len == literal_len &&
         strncmp(block_tag.start, literal, literal_len) == 0;
}

// A block tag that cannot be proven via the beacon chain: `"pending"` has no beacon block and
// `"earliest"` (genesis / block 0) predates the beacon chain. Such requests must fall back to a
// direct RPC call instead of proof generation/verification.
bool eth_json_is_unproofable_tag(json_t block_tag) {
  return eth_json_is_tag(block_tag, "\"pending\"", sizeof("\"pending\"") - 1) ||
         eth_json_is_tag(block_tag, "\"earliest\"", sizeof("\"earliest\"") - 1);
}

bool eth_check_requested_block(verify_ctx_t* ctx, json_t req_block, bytes_t el_header, const bytes32_t block_hash) {
  if (req_block.type != JSON_TYPE_STRING || req_block.len < 3) RETURN_VERIFY_ERROR(ctx, "missing or unsupported block identifier");
  if (eth_json_is_unproofable_tag(req_block)) RETURN_VERIFY_ERROR(ctx, "block tag not provable");
  if (eth_json_is_latest(req_block) ||
      eth_json_is_tag(req_block, "\"safe\"", sizeof("\"safe\"") - 1) ||
      eth_json_is_tag(req_block, "\"finalized\"", sizeof("\"finalized\"") - 1)) return true;

  CHECK_JSON_VERIFY(req_block, req_block.len == 68 ? "bytes32" : "hexuint", "block identifier: ");

  if (req_block.len == 68) { // hash
    bytes32_t hash = {0};
    buffer_t  buf  = stack_buffer(hash);
    if (json_as_bytes(req_block, &buf).len != 32 || memcmp(hash, block_hash, 32) != 0) RETURN_VERIFY_ERROR(ctx, "blockhash mismatch");
    return true;
  }

  // a block number has at most 16 nibbles: quotes + "0x" + nibbles
  if (req_block.len > 20) RETURN_VERIFY_ERROR(ctx, "invalid block number");
  if (eth_el_header_get_uint64(el_header, EL_BLOCK_NUMBER) != json_as_uint64(req_block)) RETURN_VERIFY_ERROR(ctx, "blocknumber mismatch");
  return true;
}

// Returns true if the request targets an unprovable block tag (`"pending"`/`"earliest"`),
// in which case the method must be treated as `METHOD_UNPROOFABLE`.
static bool eth_has_unproofable_block_tag(char* method, json_t params) {
  int idx = eth_block_tag_index(method);
  if (idx == -1) return false;
  if (idx == -2) {
    json_t filter = json_at(params, 0);
    return eth_json_is_unproofable_tag(json_get(filter, "fromBlock")) ||
           eth_json_is_unproofable_tag(json_get(filter, "toBlock"));
  }
  return eth_json_is_unproofable_tag(json_at(params, idx));
}

method_type_t c4_eth_classify_method(char* method, json_t params, verify_flags_t flags) {
  for (int i = 0; i < sizeof(proofable_methods) / sizeof(proofable_methods[0]); i++) {
    if (strcmp(method, proofable_methods[i]) == 0) {
      // Unprovable block tags (pending/earliest) fall back to a direct RPC call.
      if (eth_has_unproofable_block_tag(method, params))
        return METHOD_UNPROOFABLE;
      if (strcmp(method, "eth_estimateGas") == 0)
        return ((flags & VERIFY_FLAG_PAP) ? METHOD_LOCAL : METHOD_UNPROOFABLE);
#ifdef PAP
      if (flags & VERIFY_FLAG_PAP && is_call_method(method))
        return METHOD_LOCAL;
      if (flags & VERIFY_FLAG_PAP && is_pap_tx_method(method))
        return METHOD_LOCAL;
#endif
      return METHOD_PROOFABLE;
    }
  }
  for (int i = 0; i < sizeof(local_methods) / sizeof(local_methods[0]); i++) {
    if (strcmp(method, local_methods[i]) == 0) return METHOD_LOCAL;
  }
#ifdef PAP
  if ((flags & VERIFY_FLAG_PAP) &&
      (strcmp(method, "eth_sendRawTransaction") == 0 || strcmp(method, "eth_sendTransaction") == 0))
    return METHOD_LOCAL;
#endif
  for (int i = 0; i < sizeof(not_verifieable_yet_methods) / sizeof(not_verifieable_yet_methods[0]); i++) {
    if (strcmp(method, not_verifieable_yet_methods[i]) == 0) return METHOD_UNPROOFABLE;
  }
  return METHOD_UNDEFINED;
}

method_type_t c4_eth_get_method_type(chain_id_t chain_id, char* method, json_t params, verify_flags_t flags) {
  if (c4_chain_type(chain_id) != C4_CHAIN_TYPE_ETHEREUM) return METHOD_UNDEFINED;
  return c4_eth_classify_method(method, params, flags);
}

bool c4_eth_get_prover_payload(chain_id_t chain_id, const char* method, const char* params,
                               verify_flags_t flags, buffer_t* method_out, buffer_t* params_out) {
  if (c4_chain_type(chain_id) != C4_CHAIN_TYPE_ETHEREUM) return false;
  json_t arr = json_parse((char*) params);

  if (strcmp(method, "eth_verifyLogs") == 0) {
    if (arr.type == JSON_TYPE_ARRAY) {
      bprintf(params_out, "[");
      for (int i = 0; i < json_len(arr); i++) {
        if (i > 0) bprintf(params_out, ",");
        json_t item     = json_at(arr, i);
        json_t tx_index = json_get(item, "transactionIndex");
        json_t block_nr = json_get(item, "blockNumber");
        bprintf(params_out, "{\"transactionIndex\":%j,\"blockNumber\":%j}", tx_index, block_nr);
      }
      bprintf(params_out, "]");
    }
  }
  else if ((flags & VERIFY_FLAG_PAP) && arr.type == JSON_TYPE_ARRAY) {
    if ((strcmp(method, "eth_getBalance") == 0 || strcmp(method, "eth_getTransactionCount") == 0) && json_len(arr) == 2) {
      bprintf(method_out, "eth_getProof");
      bprintf(params_out, "[%J,[],%J]", json_at(arr, 0), json_at(arr, 1));
    }
    else if (strcmp(method, "eth_getStorageAt") == 0 && json_len(arr) == 3) {
      bprintf(method_out, "eth_getProof");
      bprintf(params_out, "[%J,[%J],%J]", json_at(arr, 0), json_at(arr, 1), json_at(arr, 2));
    }
#if defined(ETH_LOGS) && defined(PAP)
    else if (strcmp(method, "eth_getLogs") == 0 && json_len(arr) >= 1) {
      json_t  filter = json_at(arr, 0);
      bytes_t blooms = c4_eth_create_bloomfilter(filter);
      if (blooms.len > 0) {
        int     count = (int) (blooms.len / 256);
        bool    first = true;
        bytes_t pname = {0};
        bprintf(params_out, "[{");
        json_for_each_property(filter, val, pname) {
          if (bytes_eq(pname, bytes("address", 7)) || bytes_eq(pname, bytes("topics", 6))) continue;
          if (!first) bprintf(params_out, ",");
          bprintf(params_out, "\"%r\":%J", pname, val);
          first = false;
        }
        if (!first) bprintf(params_out, ",");
        bprintf(params_out, "\"bloomFilter\":[");
        for (int i = 0; i < count; i++) {
          if (i) bprintf(params_out, ",");
          bprintf(params_out, "\"0x%x\"", bytes(blooms.data + i * 256, 256));
        }
        bprintf(params_out, "]}]");
        safe_free(blooms.data);
      }
    }
#endif
  }

  return true;
}

const ssz_def_t* c4_eth_get_request_type(chain_type_t chain_type) {
  return chain_type == C4_CHAIN_TYPE_ETHEREUM ? eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST) : NULL;
}

// Reject proofs whose container does not match the proof type the requested
// method was proven for. The dispatcher below routes on the *attacker-controlled* union
// tag alone, so without this gate a proof for method A could be verified as method B.
// Returns `false` when a hard mismatch is found (caller returns immediately, error set).
static bool c4_eth_check_proof_type(verify_ctx_t* ctx) {
  eth_ssz_type_t expected = eth_expected_proof_type(ctx);
  if (expected == 0) return true; // local / unproofable / unknown: nothing to bind here

  const ssz_def_t* expected_def = eth_ssz_verification_type(expected);
  if (ssz_is_type(&ctx->proof, expected_def)) return true;

  // When logs completeness is required, the legitimate proof is the completeness
  // variant, not the plain logs proof.
  if (expected == ETH_SSZ_VERIFY_LOGS_PROOF &&
      (ctx->flags & VERIFY_FLAG_LOGS_COMPLETENESS) &&
      ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_LOGS_COMPLETENESS_PROOF)))
    return true;

  // No-proof variants (`SSZ_TYPE_NONE`) are handled inside the dispatcher: PAP-local
  // paths, call paths, nullable and sync-data-only paths.
  if (no_proof(ctx)) return true;

  ctx->state.error = bprintf(NULL, "proof type mismatch: method '%s' requires proof type %d, not the supplied proof",
                             ctx->method, (int) expected);
  ctx->success     = false;
  return false;
}

bool c4_eth_dispatch_proof(verify_ctx_t* ctx) {
  if (!c4_eth_check_proof_type(ctx)) return true;

#ifdef ETH_TX
  if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_TRANSACTION_PROOF)))
    verify_tx_proof(ctx);
  else
#endif
#ifdef ETH_RECEIPT
      if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_RECEIPT_PROOF)))
    verify_receipt_proof(ctx);
  else if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_BLOCK_RECEIPTS_PROOF)))
    verify_block_receipts_proof(ctx);
  else
#endif
#ifdef ETH_LOGS
      if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_LOGS_COMPLETENESS_PROOF)))
    verify_logs_completeness(ctx);
  else if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_LOGS_PROOF))) {
    // When completeness is required, a plain (per-log) logs proof is not sufficient.
    if (ctx->flags & VERIFY_FLAG_LOGS_COMPLETENESS) {
      ctx->state.error = strdup("logs completeness required, but the proof is a plain logs proof");
      ctx->success     = false;
    }
    else
      verify_logs_proof(ctx);
  }
  else
#endif
#ifdef ETH_ACCOUNT
      if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_ACCOUNT_PROOF)))
    verify_account_proof(ctx);
  else
#endif
#ifdef ETH_CALL
      if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_CALL_PROOF)) ||
          (no_proof(ctx) && is_call_method(ctx->method)))
    verify_call_proof(ctx);
  else
#endif
#ifdef ETH_BLOCK
      if (ssz_is_type(&ctx->proof, eth_ssz_verification_type(ETH_SSZ_VERIFY_BLOCK_PROOF)))
    verify_block_proof(ctx);
  else
#endif
#ifdef PAP
      if ((ctx->flags & VERIFY_FLAG_PAP) && is_pap_tx_method(ctx->method) && no_proof(ctx))
    verify_pap_tx(ctx);
  else
#endif
#ifdef ETH_UTIL
      if (c4_eth_classify_method(ctx->method, ctx->args, ctx->flags) == METHOD_LOCAL)
    verify_eth_local(ctx);
  else
#endif
      if (ctx->method == NULL && ctx->proof.def->type == SSZ_TYPE_NONE && ctx->sync_data.def->type != SSZ_TYPE_NONE && ctx->data.def->type == SSZ_TYPE_NONE)
    ctx->success = true; // if you only verify the sync data, this is ok
  else if (ctx->proof.def->type == SSZ_TYPE_NONE && ctx->sync_data.def->type == SSZ_TYPE_NONE && ctx->data.def->type == SSZ_TYPE_NONE && is_nullable_method(ctx->method))
    ctx->success = true; // this means there is simply nothing to verify.
  else {
    ctx->state.error = strdup("proof is not a supported proof type or not enabled");
    ctx->success     = false;
  }
  return true;
}

bool c4_eth_verify_request(verify_ctx_t* ctx) {
  // Pending requests (for example checkpointz) stay in ctx->state for c4_verify().
  if (c4_update_from_sync_data(ctx) != C4_SUCCESS) return true;
  return c4_eth_dispatch_proof(ctx);
}

bool c4_eth_verify(verify_ctx_t* ctx) {
  if (c4_chain_type(ctx->chain_id) != C4_CHAIN_TYPE_ETHEREUM || c4_eth_get_chain_spec(ctx->chain_id) == NULL) return false;
  return c4_eth_verify_request(ctx);
}

void c4_eth_reset_caches(void) {
  c4_header_cache_clear();
#ifdef PAP
  pap_tx_cache_reset();
#endif
}
