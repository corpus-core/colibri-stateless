set(VERIFIER_SOURCES
  verifier/sync_committee.c
  verifier/sync_committee_state.c
  verifier/verify_blockhash_proof.c
  verifier/verify_account_proof.c
  verifier/verify_tx_proof.c
  verifier/verify_receipt_proof.c
  verifier/verify_logs_proof.c
  verifier/verify_call.c
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

if(EVMONE)
  add_subdirectory(precompiles)
  set(VERIFIER_SOURCES ${VERIFIER_SOURCES}  verifier/call_evmone.c)
  set(VERIFIER_DEPS util evmone_wrapper eth_precompiles)
endif()


add_verifier(
  NAME eth_verifier
  SOURCES ${VERIFIER_SOURCES}
  DEPENDS ${VERIFIER_DEPS}
  GET_REQ_TYPE c4_eth_get_request_type
  VERIFY c4_eth_verify
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
    proofer/beacon_cache.c
    proofer/eth_tools.c
    proofer/proof_sync.c
  DEPENDS util eth_verifier
  PROOF eth_proofer_execute
)

