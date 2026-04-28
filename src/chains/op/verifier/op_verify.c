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
#include "op_types.h"
#include "ssz.h"
#include "sync_committee.h"
#include "verify.h"
#include <string.h>

// : OP-Stack

// :: Supported RPC-Methods
//
// The following table shows the supported RPC-Methods for the Ethereum Execution Proofs.
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
    RPC_METHOD("eth_blockNumber", Uint256, EthBlockNumberProof),
    RPC_METHOD("eth_getBlockHeader", EthBlockHeaderData, EthBlockHeaderProof),
    RPC_METHOD("eth_blobBaseFee", Uint256, EthBlockHeaderProof),
    RPC_METHOD("eth_maxPriorityFeePerGas", Uint256, EthBlockHeaderProof),
    RPC_METHOD("eth_newPendingTransactionFilter", Void, Void),
    RPC_METHOD("eth_newFilter", Void, Void),
    RPC_METHOD("eth_newBlockFilter", Void, Void),
    RPC_METHOD("eth_getFilterChanges", Void, Void),
    RPC_METHOD("eth_getFilterLogs", Void, Void),
    RPC_METHOD("eth_uninstallFilter", Uint256, Void),
    RPC_METHOD("eth_subscribe", Uint256, Void),
    RPC_METHOD("eth_unsubscribe", Uint256, Void),
};
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

#ifdef PAP
static bool is_call_method(char* method) {
  return strcmp(method, "eth_call") == 0 || strcmp(method, "eth_estimateGas") == 0 || strcmp(method, "colibri_simulateTransaction") == 0;
}
static bool is_pap_tx_method(char* method) {
  return strcmp(method, "eth_getTransactionReceipt") == 0 ||
         strcmp(method, "eth_getTransactionByHash") == 0 ||
         strcmp(method, "eth_getTransactionByBlockNumberAndIndex") == 0 ||
         strcmp(method, "eth_sendRawTransaction") == 0 ||
         strcmp(method, "eth_sendTransaction") == 0;
}
#endif

method_type_t c4_op_get_method_type(chain_id_t chain_id, char* method, json_t params, verify_flags_t flags) {
  (void) params;
  if (c4_chain_type(chain_id) != C4_CHAIN_TYPE_OP) return METHOD_UNDEFINED;

  for (int i = 0; i < sizeof(proofable_methods) / sizeof(proofable_methods[0]); i++) {
    if (strcmp(method, proofable_methods[i]) == 0) {
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

const ssz_def_t* c4_op_get_request_type(chain_type_t chain_type) {
  return chain_type == C4_CHAIN_TYPE_OP ? op_ssz_verification_type(OP_SSZ_VERIFY_REQUEST) : NULL;
}
extern bool verify_eth_local(verify_ctx_t* ctx);

bool c4_op_verify(verify_ctx_t* ctx) {
  if (c4_chain_type(ctx->chain_id) != C4_CHAIN_TYPE_OP) return false;
  if (c4_eth_get_chain_spec(ctx->chain_id) == NULL) return false;
  if (!c4_update_from_sync_data(ctx)) return true;

  /* Legacy OP block proof wrapping preconfirmation data (non-hybrid). */
  if (ssz_is_type(&ctx->proof, op_ssz_verification_type(OP_SSZ_VERIFY_BLOCK_PROOF))) {
    op_verify_block(ctx);
    return true;
  }

  /* Execution-layer proofs share the Ethereum verifier (hybrid + full CL proofs); hybrid SSZ layouts live in `verify_types.c`. */
  return c4_eth_dispatch_execution_proof(ctx);
}
