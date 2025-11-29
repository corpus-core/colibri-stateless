if(USE_MCL)
    add_library(eth_bn254 bn254_mcl.c)
    target_link_libraries(eth_bn254 PUBLIC util intx_wrapper mcl_interface)
else()
    add_library(eth_bn254 bn254.c)
    target_link_libraries(eth_bn254 PUBLIC util intx_wrapper)
endif()

target_include_directories(eth_bn254 PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libs/intx
    ${CMAKE_SOURCE_DIR}/src/util
)

