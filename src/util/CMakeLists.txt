option(FILE_STORAGE "if activated the verfifier will use a simple file-implementaion to store states in the current folder or in a folder specified by the env varC4_STATE_DIR" ON)
if(FILE_STORAGE)
    add_definitions(-DFILE_STORAGE)
endif()


option(PRECOMPILE_ZERO_HASHES "if activated zero hashes are cached which costs up to 1kb in RAM, but are needed in order to calc BeaconBodys in the proofer, but not in the client.in" ON)
if(PRECOMPILE_ZERO_HASHES)
    add_definitions(-DPRECOMPILE_ZERO_HASHES)
endif()

add_library(util STATIC 
  bytes.c 
  ssz.c
  crypto.c
  plugin.c
  patricia.c
  rlp.c
  json.c
)
target_include_directories(util PRIVATE ../../libs/crypto)
target_link_libraries(util PRIVATE crypto blst)
