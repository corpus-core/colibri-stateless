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

add_library(colibri_dart SHARED
    force_link.c
)

set_target_properties(colibri_dart PROPERTIES
    OUTPUT_NAME "colibri"
)

if(WIN32)
    set_target_properties(colibri_dart PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

target_link_libraries(colibri_dart
    PRIVATE
    api_ffi
    util
    prover
    verifier
    eth_verifier
    blst
    crypto
)

add_custom_command(TARGET colibri_dart POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:colibri_dart> ${CMAKE_SOURCE_DIR}/bindings/dart/native/
    COMMENT "Copying Dart native library to bindings/dart/native"
)
