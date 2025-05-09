option(ETH_ACCOUNT  "support eth account verification. eth_getBalance, eth_getStorageAt, eth_getProof, eth_getCode, eth_getTransactionCount" ON)
option(ETH_TX  "support eth Transaction verification. eth_getTransactionByHash, eth_getTransactionByBlockHashAndIndex, eth_getTransactionByBlockNumberAndIndex" ON)
option(ETH_BLOCK  "support eth block verification. eth_getBlockByHash, eth_getBlockByNumber, eth_getBlockTransactionCountByHash, eth_getBlockTransactionCountByNumber, eth_getUncleCountByBlockHash, eth_getUncleCountByBlockNumber" ON)
option(ETH_RECEIPT  "support eth receipt verification. eth_getTransactionReceipt" ON)
option(ETH_LOGS  "support eth logs verification. eth_getLogs" ON)
option(ETH_CALL  "support eth call verification. eth_call, eth_estimateGas" ON)
option(ETH_UTIL  "support eth utils like eth_chainId or web3_sha3" ON)


option(EVMONE "uses evmone to verify eth_calls" ON)
option(EVMLIGHT "uses evmlight vor eth_call verification, which is smaller and faster, but does not track gas." OFF)
# precompiles
option(PRECOMPILES_RIPEMD160 "Precompile ripemd160" ON)



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
endif()


set(VERIFIER_SOURCES
  verifier/sync_committee.c
  verifier/sync_committee_state.c
  verifier/verify_blockhash_proof.c
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

if(ETH_CALL)
  set(VERIFIER_SOURCES ${VERIFIER_SOURCES} verifier/verify_call.c)
  add_definitions(-DETH_CALL)

  if(EVMONE)
    add_subdirectory(precompiles)
    set(VERIFIER_SOURCES ${VERIFIER_SOURCES}  verifier/call_evmone.c)
    set(VERIFIER_DEPS util evmone_wrapper eth_precompiles)
  endif()
  
endif()
if(ETH_UTIL)
  set(VERIFIER_SOURCES ${VERIFIER_SOURCES} verifier/verify_local.c)
  add_definitions(-DETH_UTIL)
endif()

add_verifier(
  NAME eth_verifier
  SOURCES ${VERIFIER_SOURCES}
  DEPENDS ${VERIFIER_DEPS}
  GET_REQ_TYPE c4_eth_get_request_type
  VERIFY c4_eth_verify
  METHOD_TYPE c4_eth_get_method_type
)

add_proofer(
  NAME eth_proofer
  SOURCES 
    proofer/eth_proofer.c
    proofer/proof_account.c
    proofer/proof_transaction.c
    proofer/proof_logs.c
    proofer/proof_call.c
    proofer/beacon.c
    proofer/proof_receipt.c
    proofer/eth_req.c
    proofer/eth_tools.c
    proofer/proof_sync.c
    proofer/proof_block.c
    proofer/historic_proof.c
  DEPENDS util eth_verifier
  PROOF eth_proofer_execute
)

