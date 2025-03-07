add_library(verifier STATIC 
  verify.c
)
target_include_directories(verifier INTERFACE .)
target_link_libraries(verifier PUBLIC   util)
