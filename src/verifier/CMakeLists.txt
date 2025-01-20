add_library(verifier STATIC 
  beacon_header.c 
  beacon_types.c
  sync_committee.c
)
target_include_directories(verifier PRIVATE util)
