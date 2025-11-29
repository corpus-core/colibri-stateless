if(ETH_ZKPROOF)
    add_library(zk_verifier STATIC zk_verifier.c)
    
    # Link against eth_bn254 and util
    target_link_libraries(zk_verifier PUBLIC eth_bn254 util crypto)
    
    # Export include directory for other targets (e.g. tests)
    target_include_directories(zk_verifier PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

    # CLI Tool for quick manual testing
    add_executable(verify_zk_proof_cli main.c)
    target_link_libraries(verify_zk_proof_cli PRIVATE zk_verifier)
endif()

