add_library(verifier STATIC 
  verify.c
)
target_include_directories(verifier PRIVATE util eth_verifier)

target_link_libraries(verifier PRIVATE   util)