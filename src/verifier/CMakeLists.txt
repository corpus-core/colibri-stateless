add_library(verifier STATIC 
  types_beacon.c
  types_verify.c
  sync_committee.c
  verify.c
  verify_blockhash_proof.c
)
target_include_directories(verifier PRIVATE util)

