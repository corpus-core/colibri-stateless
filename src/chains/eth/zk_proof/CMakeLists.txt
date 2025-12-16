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

if(CARGO_EXECUTABLE)
  add_custom_command(
    OUTPUT ${ZKPROOF_BIN_DIR}/eth-sync-script.stamp
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ZKPROOF_BIN_DIR}
    COMMAND ${CMAKE_COMMAND} -E env CARGO_NET_GIT_FETCH_WITH_CLI=true
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
  message(WARNING "Cargo not found - eth-sync-script will not be built. Copying frozen eth_sync_program only.")
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

