option(FILE_STORAGE "if activated the verfifier will use a simple file-implementaion to store states in the current folder or in a folder specified by the env varC4_STATE_DIR" ON)
if(FILE_STORAGE AND NOT WASM)
    add_definitions(-DFILE_STORAGE)
endif()

option(MESSAGES "if activated the binaries will contain error messages, but for embedded systems this is not needed and can be turned off to save memory" ON)
if(MESSAGES)
    add_definitions(-DMESSAGES)
endif()

option(PRECOMPILE_ZERO_HASHES "if activated zero hashes are cached which costs up to 1kb in RAM, but are needed in order to calc BeaconBodys in the proofer, but not in the verfier" ON)
if(PRECOMPILE_ZERO_HASHES)
    add_definitions(-DPRECOMPILE_ZERO_HASHES)
endif()

add_library(util STATIC 
  bytes.c 
  ssz.c
  ssz_merkle.c
  ssz_builder.c
  crypto.c
  plugin.c
  json.c
  json_validate.c
  logger.c
  chains.c
  state.c
  version.c
)
target_include_directories(util PRIVATE ../../libs/crypto ${CMAKE_BINARY_DIR}/_deps/blst-src/bindings)
target_include_directories(util INTERFACE .)
target_link_libraries(util PRIVATE crypto blst)
