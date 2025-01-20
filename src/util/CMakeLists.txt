add_library(util STATIC 
  bytes.c 
  ssz.c
  crypto.c
)
target_include_directories(util PRIVATE ../../libs/crypto)
target_link_libraries(util PRIVATE crypto )
target_link_libraries(util PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/blst/bin/libblst.a")
