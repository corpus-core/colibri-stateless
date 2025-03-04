add_library(proofer STATIC 
  proofer.c
  proof_account.c
  proof_transaction.c
  proof_logs.c
  ssz_types.c
  beacon.c
  proof_receipt.c
  eth_req.c
  beacon_cache.c
)
target_include_directories(proofer PRIVATE util)
target_link_libraries(proofer PRIVATE  
   util
   verifier
)
