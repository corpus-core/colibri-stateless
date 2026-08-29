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

#include "op_verify.h"
#include "beacon_types.h"
#include "chains.h"
#include "eth_verify.h"
#include "json.h"
#include "ssz.h"
#include "verify.h"
#include <string.h>

// : OP-Stack
//
// OP-Stack proofs use the same `C4Request` / proof unions as Ethereum. The only
// OP-specific wire difference is `ETH_BLOCK_PROOF_UNION` index 2 (`sequencerProof`).

static const char* proofable_methods[] = {
    RPC_METHOD("eth_call", Bytes, EthCallProof),
    RPC_METHOD("colibri_simulateTransaction", EthSimulationResult, EthCallProof),
    RPC_METHOD("eth_getProof", EthProofData, EthAccountProof),
    RPC_METHOD("eth_getBalance", Uint256, EthAccountProof),
    RPC_METHOD("eth_getBlockByHash", EthBlockData, EthBlockProof),
    RPC_METHOD("eth_getBlockByNumber", EthBlockData, EthBlockProof),
    RPC_METHOD("eth_getBlockHeader", EthBlockHeaderData, EthBlockHeaderProof),
    RPC_METHOD("eth_getCode", Bytes, EthAccountProof),
    RPC_METHOD("eth_getLogs", ListEthReceiptDataLog, ListEthLogsBlock),
    RPC_METHOD("eth_verifyLogs", Void, ListEthLogsBlock),
    RPC_METHOD("eth_getTransactionCount", Uint256, EthAccountProof),
    RPC_METHOD("eth_getStorageAt", Bytes32, EthAccountProof),
    RPC_METHOD("eth_getTransactionReceipt", EthReceiptData, EthReceiptProof),
    RPC_METHOD("eth_getTransactionByHash", EthTxData, EthTransactionProof),
    RPC_METHOD("eth_getTransactionByBlockHashAndIndex", EthTxData, EthTransactionProof),
    RPC_METHOD("eth_getTransactionByBlockNumberAndIndex", EthTxData, EthTransactionProof),
    RPC_METHOD("eth_blockNumber", Uint256, EthBlockNumberProof),
    RPC_METHOD("eth_newPendingTransactionFilter", Void, Void),
    RPC_METHOD("eth_newFilter", Void, Void),
    RPC_METHOD("eth_newBlockFilter", Void, Void),
    RPC_METHOD("eth_getFilterChanges", Void, Void),
    RPC_METHOD("eth_getFilterLogs", Void, Void),
    RPC_METHOD("eth_uninstallFilter", Void, Void),
    RPC_METHOD("eth_subscribe", Uint256, Void),
    RPC_METHOD("eth_unsubscribe", Void, Void),
};
static const char* local_methods[] = {
    RPC_METHOD("eth_chainId", Uint64, Void),
    RPC_METHOD("eth_accounts", ListAddress, Void),
    RPC_METHOD("eth_protocolVersion", Uint256, Void),
    RPC_METHOD("web3_clientVersion", String, Void),
    RPC_METHOD("web3_sha3", Bytes32, Void),
    RPC_METHOD("colibri_decodeTransaction", EthTxData, Void),
};

static const char* not_verifieable_yet_methods[] = {
    RPC_METHOD("eth_getUncleByBlockHashAndIndex", Void, Void),
    RPC_METHOD("eth_getUncleByBlockNumberAndIndex", Void, Void),
    RPC_METHOD("eth_getBlockTransactionCountByHash", Void, Void),
    RPC_METHOD("eth_getBlockTransactionCountByNumber", Void, Void),
    RPC_METHOD("eth_feeHistory", Void, Void),
    RPC_METHOD("eth_blobBaseFee", Uint64, EthBlockHeaderProof),
    RPC_METHOD("eth_createAccessList", EthAccessData, EthCallProof),
    RPC_METHOD("eth_estimateGas", Uint64, EthCallProof),
    RPC_METHOD("eth_gasPrice", Void, Void),
    RPC_METHOD("eth_getBlockReceipts", Void, Void),
    RPC_METHOD("eth_getUncleByBlockHash", Void, Void),
    RPC_METHOD("eth_getUncleByBlockNumber", Void, Void),
    RPC_METHOD("eth_getUncleCountByBlockHash", Void, Void),
    RPC_METHOD("eth_getUncleCountByBlockNumber", Void, Void),
    RPC_METHOD("eth_maxPriorityFeePerGas", Void, Void),
    RPC_METHOD("eth_sendRawTransaction", Void, Void),
};

static bool eth_json_is_tag(json_t block_tag, const char* literal, size_t literal_len) {
  return block_tag.type == JSON_TYPE_STRING &&
         block_tag.len == literal_len &&
         strncmp(block_tag.start, literal, literal_len) == 0;
}

static bool op_has_unproofable_block_tag(char* method, json_t params) {
  int idx = -1;
  if (strcmp(method, "eth_getStorageAt") == 0 || strcmp(method, "eth_getProof") == 0) idx = 2;
  else if (strcmp(method, "eth_call") == 0 || strcmp(method, "colibri_simulateTransaction") == 0 ||
           strcmp(method, "eth_getBalance") == 0 || strcmp(method, "eth_getCode") == 0 ||
           strcmp(method, "eth_getTransactionCount") == 0)
    idx = 1;
  else if (strcmp(method, "eth_getBlockByNumber") == 0 || strcmp(method, "eth_getBlockHeader") == 0 ||
           strcmp(method, "eth_getTransactionByBlockNumberAndIndex") == 0)
    idx = 0;
  if (idx < 0) return false;
  json_t tag = json_at(params, idx);
  return eth_json_is_tag(tag, "\"pending\"", sizeof("\"pending\"") - 1) ||
         eth_json_is_tag(tag, "\"earliest\"", sizeof("\"earliest\"") - 1);
}

method_type_t c4_op_get_method_type(chain_id_t chain_id, char* method, json_t params, verify_flags_t flags) {
  (void) flags;
  if (c4_chain_type(chain_id) != C4_CHAIN_TYPE_OP) return METHOD_UNDEFINED;
  for (int i = 0; i < (int) (sizeof(proofable_methods) / sizeof(proofable_methods[0])); i++) {
    if (strcmp(method, proofable_methods[i]) == 0) {
      if (op_has_unproofable_block_tag(method, params)) return METHOD_UNPROOFABLE;
      return METHOD_PROOFABLE;
    }
  }
  for (int i = 0; i < (int) (sizeof(local_methods) / sizeof(local_methods[0])); i++) {
    if (strcmp(method, local_methods[i]) == 0) return METHOD_LOCAL;
  }
  for (int i = 0; i < (int) (sizeof(not_verifieable_yet_methods) / sizeof(not_verifieable_yet_methods[0])); i++) {
    if (strcmp(method, not_verifieable_yet_methods[i]) == 0) return METHOD_UNPROOFABLE;
  }
  return METHOD_UNDEFINED;
}

const ssz_def_t* c4_op_get_request_type(chain_type_t chain_type) {
  return chain_type == C4_CHAIN_TYPE_OP ? eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST) : NULL;
}

void op_init_rpc_ctx(c4_init_ctx_t* ctx) {
  if (!ctx || c4_chain_type(ctx->chain_id) != C4_CHAIN_TYPE_OP) return;
  op_register_block_proof_verify();
}

bool c4_op_verify(verify_ctx_t* ctx) {
  if (c4_chain_type(ctx->chain_id) != C4_CHAIN_TYPE_OP) return false;
  op_register_block_proof_verify();

  if (c4_op_get_method_type(ctx->chain_id, ctx->method, ctx->args, ctx->flags) == METHOD_LOCAL) {
    verify_eth_local(ctx);
    return true;
  }
  return c4_eth_dispatch_proof(ctx);
}
