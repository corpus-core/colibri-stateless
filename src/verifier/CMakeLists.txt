add_library(verifier STATIC 
  beacon_types.c
  sync_committee.c
  verify.c
  verify_blockhash_proof.c
)
target_include_directories(verifier PRIVATE util)

