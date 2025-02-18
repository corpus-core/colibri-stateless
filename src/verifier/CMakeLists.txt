add_library(verifier STATIC 
  types_beacon.c
  types_verify.c
  sync_committee.c
  sync_committee_state.c
  verify.c
  verify_blockhash_proof.c
  verify_account_proof.c
  verify_tx_proof.c
  verify_receipt_proof.c
  verify_logs_proof.c
  eth_tx.c
)
target_include_directories(verifier PRIVATE util)

target_link_libraries(verifier PRIVATE   util)