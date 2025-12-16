cmake_minimum_required(VERSION 3.16)

# Build/copy ETH ZK-Proof host artifacts next to colibri-server.
#
# This is kept inside the ETH chain folder so it can be enabled/disabled together
# with ETH_ZKPROOF, and the root server build can simply depend on the target.

if(NOT ETH_ZKPROOF OR NOT HTTP_SERVER)
  return()
endif()

set(ZKPROOF_BIN_DIR ${CMAKE_BINARY_DIR}/bin)
set(ZKPROOF_ROOT ${CMAKE_SOURCE_DIR}/src/chains/eth/zk_proof)
set(ZKPROOF_HOST_CRATE_DIR ${ZKPROOF_ROOT}/script)
set(ZKPROOF_HOST_BIN_SRC ${ZKPROOF_ROOT}/target/release/eth-sync-script)
set(ZKPROOF_HOST_BIN_DST ${ZKPROOF_BIN_DIR}/eth-sync-script)
set(ZKPROOF_GUEST_ELF_SRC ${ZKPROOF_ROOT}/program/elf/eth_sync_program)
set(ZKPROOF_GUEST_ELF_DST ${ZKPROOF_BIN_DIR}/eth_sync_program)

find_program(CARGO_EXECUTABLE cargo)

# sp1-recursion-core (pulled in by eth-sync-script) compiles C++ code via cc-rs and
# requires C++20 features (designated initializers). When enabled, we force C++20
# explicitly via environment flags for the cargo build.
if(MSVC)
  set(ZKPROOF_CXXFLAGS_ENV "CXXFLAGS=-std:c++20")
  set(ZKPROOF_CL_ENV "CL=-std:c++20")
else()
  set(ZKPROOF_CXXFLAGS_ENV "CXXFLAGS=-std=c++20")
  set(ZKPROOF_CL_ENV "")
endif()

if(CARGO_EXECUTABLE AND ETH_ZKPROOF_BUILD_HOST)
  add_custom_command(
    OUTPUT ${ZKPROOF_BIN_DIR}/eth-sync-script.stamp
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ZKPROOF_BIN_DIR}
    COMMAND ${CMAKE_COMMAND} -E env CARGO_NET_GIT_FETCH_WITH_CLI=true
            ${ZKPROOF_CXXFLAGS_ENV}
            ${ZKPROOF_CL_ENV}
            ${CARGO_EXECUTABLE} build --release
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ZKPROOF_HOST_BIN_SRC}
            ${ZKPROOF_HOST_BIN_DST}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ZKPROOF_GUEST_ELF_SRC}
            ${ZKPROOF_GUEST_ELF_DST}
    COMMAND ${CMAKE_COMMAND} -E touch ${ZKPROOF_BIN_DIR}/eth-sync-script.stamp
    WORKING_DIRECTORY ${ZKPROOF_HOST_CRATE_DIR}
    COMMENT "Building eth-sync-script and copying ZK artifacts"
    VERBATIM
  )
  add_custom_target(eth_zkproof_artifacts DEPENDS ${ZKPROOF_BIN_DIR}/eth-sync-script.stamp)
else()
  if(MSVC AND CARGO_EXECUTABLE AND NOT ETH_ZKPROOF_BUILD_HOST)
    message(WARNING "MSVC build: skipping eth-sync-script build (sp1 C++ deps require C++20). Copying frozen eth_sync_program only. Set ETH_ZKPROOF_BUILD_HOST=ON to try building it.")
  elseif(NOT CARGO_EXECUTABLE)
    message(WARNING "Cargo not found - eth-sync-script will not be built. Copying frozen eth_sync_program only.")
  else()
    message(WARNING "ETH_ZKPROOF_BUILD_HOST disabled - eth-sync-script will not be built. Copying frozen eth_sync_program only.")
  endif()
  add_custom_command(
    OUTPUT ${ZKPROOF_BIN_DIR}/eth_sync_program.stamp
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ZKPROOF_BIN_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ZKPROOF_GUEST_ELF_SRC}
            ${ZKPROOF_GUEST_ELF_DST}
    COMMAND ${CMAKE_COMMAND} -E touch ${ZKPROOF_BIN_DIR}/eth_sync_program.stamp
    COMMENT "Copying frozen eth_sync_program next to colibri-server"
    VERBATIM
  )
  add_custom_target(eth_zkproof_artifacts DEPENDS ${ZKPROOF_BIN_DIR}/eth_sync_program.stamp)
endif()

