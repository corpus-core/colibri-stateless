add_library(util STATIC 
  bytes.c 
  ssz.c
  crypto.c
)
target_include_directories(util PRIVATE ../../libs/crypto)
target_link_libraries(util PRIVATE crypto blst)
add_dependencies(util blst_lib) 