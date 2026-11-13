# This file is included from the repo root (DART=ON). Configuring with
# `cmake -S bindings/dart` leaves CMAKE_SOURCE_DIR here and breaks includes.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/src/prover/prover.h")
  message(FATAL_ERROR
    "Configure colibri_dart from the repository root, not bindings/dart.\n"
    "  cd bindings/dart && ./build.sh\n"
    "or from the repo root:\n"
    "  cmake -S . -B build-dart -DDART=ON -DCHAIN_OP=ON -DCMAKE_BUILD_TYPE=Release\n"
    "  cmake --build build-dart --target colibri_dart")
endif()

include_directories(${CMAKE_SOURCE_DIR}/src/api)
include_directories(${CMAKE_SOURCE_DIR}/src/util)
include_directories(${CMAKE_SOURCE_DIR}/src/prover)
include_directories(${CMAKE_SOURCE_DIR}/src/verifier)

file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/bindings/dart/native)

# Compile the FFI units into this shared library instead of pulling them out of
# the static api/api_ffi archives. Archive members whose only reference is a
# __DATA_CONST pointer are dropped by some Apple linkers, so dlsym then fails
# for c4_reset_caches. Objects on the shared-library link line are always
# present; force_link.c plus -u below keep them in the export trie.
add_library(colibri_dart SHARED
    force_link.c
    ${CMAKE_SOURCE_DIR}/src/api/colibri.c
    ${CMAKE_SOURCE_DIR}/src/api/colibri_common.c
)

set_target_properties(colibri_dart PROPERTIES
    OUTPUT_NAME "colibri"
)

if(WIN32)
    set_target_properties(colibri_dart PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

target_link_libraries(colibri_dart
    PRIVATE
    util
    prover
    verifier
    eth_verifier
    blst
    crypto
)

# Symbols Dart resolves with DynamicLibrary.lookup. -u is a linker root, so
# dead_strip cannot drop them when the only C reference would be optimized away.
set(_colibri_dart_ffi_exports
    c4_create_prover_ctx
    c4_prover_execute_json_status
    c4_prover_get_proof
    c4_free_prover_ctx
    c4_req_set_response
    c4_req_set_error
    c4_verify_create_ctx
    c4_verify_execute_json_status
    c4_verify_free_ctx
    c4_get_method_support
    c4_get_l1_chain_id_for_l2
    c4_set_storage_config
    buffer_append
    c4_create_rpc_ctx
    c4_rpc_execute_json_status
    c4_free_rpc_ctx
    c4_set_checkpoint
    c4_reset_caches
    c4_rpc_set_witness_keys
    c4_rpc_set_proxy_urls
    c4_rpc_set_min_latest_block_ts
    c4_verify_set_min_latest_block_ts
)
if(APPLE)
    foreach(sym IN LISTS _colibri_dart_ffi_exports)
        target_link_options(colibri_dart PRIVATE "LINKER:-u,_${sym}")
    endforeach()
elseif(UNIX)
    foreach(sym IN LISTS _colibri_dart_ffi_exports)
        target_link_options(colibri_dart PRIVATE "LINKER:--undefined=${sym}")
    endforeach()
endif()

add_custom_command(TARGET colibri_dart POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:colibri_dart> ${CMAKE_SOURCE_DIR}/bindings/dart/native/
    COMMENT "Copying Dart native library to bindings/dart/native"
)
