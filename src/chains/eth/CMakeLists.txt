# Copyright (c) 2025 corpus.core
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of
# this software and associated documentation files (the "Software"), to deal in
# the Software without restriction, including without limitation the rights to
# use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
# the Software, and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
# FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# SPDX-License-Identifier: MIT

option(ETH_ACCOUNT  "support eth account verification. eth_getBalance, eth_getStorageAt, eth_getProof, eth_getCode, eth_getTransactionCount" ON)
option(ETH_TX  "support eth Transaction verification. eth_getTransactionByHash, eth_getTransactionByBlockHashAndIndex, eth_getTransactionByBlockNumberAndIndex" ON)
option(ETH_BLOCK  "support eth block verification. eth_getBlockByHash, eth_getBlockByNumber, eth_getBlockTransactionCountByHash, eth_getBlockTransactionCountByNumber, eth_getUncleCountByBlockHash, eth_getUncleCountByBlockNumber" ON)
option(ETH_RECEIPT  "support eth receipt verification. eth_getTransactionReceipt" ON)
option(ETH_LOGS  "support eth logs verification. eth_getLogs" ON)
option(ETH_CALL  "support eth call verification. eth_call, eth_estimateGas" ON)
option(ETH_UTIL  "support eth utils like eth_chainId or web3_sha3" ON)
option(USE_CHECKPOINTZ "enable checkpoint fetching from checkpointz (beacon node) for bootstrap and weak subjectivity validation (requires HTTP access, saves ~2-3 KB if disabled). SECURITY WARNING: Disabling this removes (1) automatic bootstrap checkpoint fetching and (2) weak subjectivity period validation, requiring manual checkpoint configuration and increasing long-range attack risk. Only disable for embedded devices without HTTP access. See: https://github.com/runtimeverification/beacon-chain-verification/blob/master/weak-subjectivity/weak-subjectivity-analysis.pdf" ON)

if(USE_CHECKPOINTZ)
    add_definitions(-DUSE_CHECKPOINTZ)
else()
    message(WARNING "⚠️  Checkpointz disabled - requires manual checkpoint configuration!")
    message(STATUS "    Bootstrap: Initial checkpoint must be provided in config")
    message(STATUS "    WSP: No weak subjectivity validation (increases long-range attack risk)")
    message(STATUS "    Saves ~2-3 KB by removing checkpoint fetching code")
    message(STATUS "    Only recommended for embedded devices without HTTP access")
    message(STATUS "    See: https://github.com/runtimeverification/beacon-chain-verification")
endif()

option(EVMONE "uses evmone to verify eth_calls" ON)
option(EVMLIGHT "uses evmlight vor eth_call verification, which is smaller and faster, but does not track gas." OFF)
# precompiles
option(PRECOMPILES_RIPEMD160 "Precompile ripemd160" ON)
option(PRECOMPILES_BN128 "Precompile BN128 (ecadd, ecmul, ecpairing)" ON)
option(PRECOMPILES_KZG "Precompile KZG (point evaluation)" ON)

if(NOT MSVC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-excess-initializers")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-excess-initializers")
endif()

if (EVMLIGHT OR EVMONE)
    if (EVMONE) 
       set(EVMLIGHT OFF)
       add_definitions(-DEVMONE)
    else()
        add_definitions(-DEVMLIGHT)
    endif()
    set(EVM ON)
    add_definitions(-DEVM -DINTX)
    set(INTX ON)

    if (EVMLIGHT)
        add_definitions(-DEVMLIGHT)
    endif()
    if (PRECOMPILES_RIPEMD160)
        add_definitions(-DPRECOMPILED_RIPEMD160)
    endif()
    if (PRECOMPILES_BN128)
        add_definitions(-DPRECOMPILED_BN128)
    endif()
    if (PRECOMPILES_KZG)
        add_definitions(-DPRECOMPILED_KZG)
    endif()
endif()


set(VERIFIER_SOURCES
  verifier/sync_committee.c
  verifier/sync_committee_state.c
  verifier/beacon_header.c
  verifier/eth_tx.c
  verifier/eth_account.c
  verifier/eth_verify.c
  verifier/patricia_trie.c
  verifier/patricia.c
  verifier/rlp.c
  ssz/verify_types.c
  ssz/beacon_types.c
  ssz/beacon_denep.c
  ssz/beacon_electra.c
)
set(VERIFIER_DEPS util)

if(ETH_ACCOUNT)
  set(VERIFIER_SOURCES ${VERIFIER_SOURCES} verifier/verify_account_proof.c)
  add_definitions(-DETH_ACCOUNT)
endif()

if(ETH_TX)
  set(VERIFIER_SOURCES ${VERIFIER_SOURCES} verifier/verify_tx_proof.c)
  add_definitions(-DETH_TX)
endif()

if(ETH_BLOCK)
  set(VERIFIER_SOURCES ${VERIFIER_SOURCES} verifier/verify_block.c)
  add_definitions(-DETH_BLOCK)
endif()

if(ETH_RECEIPT)
  set(VERIFIER_SOURCES ${VERIFIER_SOURCES} verifier/verify_receipt_proof.c)
  add_definitions(-DETH_RECEIPT)
endif()

if(ETH_LOGS)
  set(VERIFIER_SOURCES ${VERIFIER_SOURCES} verifier/verify_logs_proof.c)
  add_definitions(-DETH_LOGS)
endif()


# Add BN254 library (used by precompiles and ZK verifier)
if(PRECOMPILES_BN128 OR ETH_ZKPROOF)
    add_subdirectory(bn254)
endif()

if(ETH_CALL)
  set(VERIFIER_SOURCES ${VERIFIER_SOURCES} verifier/verify_call.c verifier/verify_simulate.c)
  add_definitions(-DETH_CALL)

  if(EVMONE)
    add_subdirectory(precompiles)
    set(VERIFIER_SOURCES ${VERIFIER_SOURCES}  verifier/call_evmone.c)
    set(VERIFIER_DEPS PUBLIC eth_precompiles evmone_wrapper )
  endif()
  
endif()
if(ETH_UTIL)
  set(VERIFIER_SOURCES ${VERIFIER_SOURCES} verifier/verify_local.c)
  add_definitions(-DETH_UTIL)
endif()

# Add ZK Verifier if enabled
if(ETH_ZKPROOF)
    add_subdirectory(zk_verifier)
    set(VERIFIER_DEPS ${VERIFIER_DEPS} zk_verifier)
endif()

add_verifier(
  NAME eth_verifier
  SOURCES ${VERIFIER_SOURCES}
  DEPENDS ${VERIFIER_DEPS}
  GET_REQ_TYPE c4_eth_get_request_type
  VERIFY c4_eth_verify
  METHOD_TYPE c4_eth_get_method_type
)

# Register ETH chain properties provider
add_chain_props(FUNC c4_eth_chain_props)

add_prover(
  NAME eth_prover
  SOURCES 
    prover/eth_prover.c
    prover/proof_account.c
    prover/proof_transaction.c
    prover/tx_cache.c
    prover/proof_logs.c
    prover/proof_call.c
    prover/beacon.c
    prover/proof_receipt.c
    prover/eth_req.c
    prover/logs_cache.c
    prover/eth_tools.c
    prover/proof_sync.c
    prover/proof_block.c
    prover/proof_witness.c
    prover/historic_proof.c
    prover/historic_proof_zk.c
  DEPENDS util eth_verifier
  PROOF eth_prover_execute
)

add_server_handler(
  NAME eth_server_handler
  SOURCES 
    server/handler.c
    server/metrics.c
    server/head_watcher.c
    server/head_update.c
    server/chain_props.c
    server/handle_lcu.c
    server/handle_headers.c
    server/client_detection.c
    server/client_mappings.c
    server/period_store.c
    server/period_store_call.c
    server/period_store_lc.c
    server/period_store_historical_roots.c
    server/period_store_backfill.c
    server/period_store_zk_prover.c
    server/handle_period_static.c
    server/eth_conf.c
  DEPENDS eth_prover
  INIT_FUNC eth_server_init
  SHUTDOWN_FUNC eth_server_shutdown
  GET_DETECTION_REQUEST_FUNC eth_get_detection_request
  PARSE_VERSION_RESPONSE_FUNC eth_parse_version_response
  GET_CLIENT_MAPPINGS_FUNC eth_get_client_mappings
  METRICS_FUNC eth_server_metrics
  CONFIG_FUNC eth_configure
)
