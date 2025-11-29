
# Unit test for BN254 library
add_executable(test_bn254 test_bn254.c)
target_link_libraries(test_bn254 PRIVATE eth_bn254 unity)

# Unit test for Ethereum Precompiles
add_executable(test_eth_precompiles test_eth_precompiles.c)
target_link_libraries(test_eth_precompiles PRIVATE eth_precompiles eth_bn254 unity util intx_wrapper)

# Integration/Unit test for ZK Proof verification
if(ETH_ZKPROOF)
    add_executable(test_zk_proof test_zk_proof.c)
    target_link_libraries(test_zk_proof PRIVATE zk_verifier unity util)
    target_compile_definitions(test_zk_proof PRIVATE ETH_ZKPROOF)
    target_include_directories(test_zk_proof PRIVATE 
        ${CMAKE_SOURCE_DIR}/src/chains/eth 
        ${CMAKE_SOURCE_DIR}/src/chains/eth/prover
        ${CMAKE_SOURCE_DIR}/src/chains/eth/server
        ${CMAKE_SOURCE_DIR}/src/chains/eth/verifier
        ${CMAKE_SOURCE_DIR}/src/chains/eth/ssz
        ${CMAKE_SOURCE_DIR}/src/util
        ${CMAKE_SOURCE_DIR}/src/prover
        ${CMAKE_SOURCE_DIR}/src/verifier
    )
endif()

# Debug tool
add_executable(debug_bn254_manual debug_bn254_manual.c)
target_link_libraries(debug_bn254_manual PRIVATE eth_bn254 util intx_wrapper)
target_include_directories(debug_bn254_manual PRIVATE 
    ${CMAKE_SOURCE_DIR}/src/chains/eth/zk_verifier
    ${CMAKE_SOURCE_DIR}/src/chains/eth/bn254
)

# Compare tool
add_executable(debug_compare_bn254 debug_compare_bn254.cpp)
target_link_libraries(debug_compare_bn254 PRIVATE mcl_interface eth_bn254 util intx_wrapper)
target_include_directories(debug_compare_bn254 PRIVATE 
    ${CMAKE_SOURCE_DIR}/src/chains/eth/bn254
)

# Debug verify MCL
add_executable(debug_verify_mcl debug_verify_mcl.cpp)
target_link_libraries(debug_verify_mcl PRIVATE mcl_interface util intx_wrapper)
target_include_directories(debug_verify_mcl PRIVATE 
    ${CMAKE_SOURCE_DIR}/src/chains/eth/zk_verifier
)
