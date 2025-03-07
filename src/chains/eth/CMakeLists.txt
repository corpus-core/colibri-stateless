if (VERIFIER)

  add_verifier(
    NAME eth_verifier
    SOURCES 
      verifier/types_beacon.c
      verifier/types_verify.c
      verifier/sync_committee.c
      verifier/sync_committee_state.c
      verifier/verify_blockhash_proof.c
      verifier/verify_account_proof.c
      verifier/verify_tx_proof.c
      verifier/verify_receipt_proof.c
      verifier/verify_logs_proof.c
      verifier/eth_tx.c
      verifier/eth_verify.c
      verifier/patricia_trie.c
      verifier/patricia.c
      verifier/rlp.c
    DEPENDS util
    GET_REQ_TYPE c4_eth_get_request_type
    VERIFY c4_eth_verify
  )

endif()

if(PROOFER)

  add_library(eth_proofer STATIC 
    proofer/eth_proofer.c
    proofer/proof_account.c
    proofer/proof_transaction.c
    proofer/proof_logs.c
    proofer/ssz_types.c
    proofer/beacon.c
    proofer/proof_receipt.c
    proofer/eth_req.c
    proofer/beacon_cache.c
  )
  target_include_directories(eth_proofer PRIVATE ../../util ../../proofer ../../verifier verifier)
  target_link_libraries(eth_proofer      PRIVATE util eth_verifier)
  target_link_libraries(proofer          PRIVATE eth_proofer)

endif()
