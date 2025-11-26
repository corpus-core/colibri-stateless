cmake_minimum_required(VERSION 3.14)

# Only build if ETH_ZKPROOF is enabled
if(ETH_ZKPROOF)
    project(zk_verifier C CXX)

    # Library
    add_library(zk_verifier STATIC zk_verifier.c)
    # mcl_interface is now available globally because it's added in libs/CMakeLists.txt
    target_link_libraries(zk_verifier PUBLIC mcl_interface)
    target_include_directories(zk_verifier PUBLIC .)

    # CLI Tool
    add_executable(verify_zk_proof_cli main.c)
    target_link_libraries(verify_zk_proof_cli PRIVATE zk_verifier)
endif()
