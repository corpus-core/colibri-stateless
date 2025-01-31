add_library(proofer STATIC 
  proofer.c
  proof_account.c
  ssz_types.c
  beacon.c
)
target_include_directories(proofer PRIVATE util)
target_link_libraries(proofer PRIVATE  
   util
   verifier
)
