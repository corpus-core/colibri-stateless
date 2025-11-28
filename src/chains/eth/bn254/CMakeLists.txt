add_library(eth_bn254
    bn254.c
)

target_include_directories(eth_bn254 PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libs/intx
    ${CMAKE_SOURCE_DIR}/src/util
)

# Link against util if needed for other things (like bytes.h if used)
# Link against intx_wrapper for big integer arithmetic
target_link_libraries(eth_bn254 PUBLIC util intx_wrapper)

