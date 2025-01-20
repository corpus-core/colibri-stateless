
# Define the output path for the static library
set(BLST_LIB_OUTPUT_DIR ${CMAKE_BINARY_DIR}/libs)
set(BLST_LIB ${BLST_LIB_OUTPUT_DIR}/libblst.a)

# Ensure the output directory exists
file(MAKE_DIRECTORY ${BLST_LIB_OUTPUT_DIR})

# Add a custom command to build the library using the script
add_custom_command(
    OUTPUT ${BLST_LIB}
    COMMAND ${CMAKE_COMMAND} -E env bash ${CMAKE_CURRENT_SOURCE_DIR}/build.sh
    COMMAND ${CMAKE_COMMAND} -E rename ${CMAKE_CURRENT_SOURCE_DIR}/libblst.a ${BLST_LIB}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building and copying libblst.a"
    VERBATIM
)

# Add a custom target that depends on the custom command
add_custom_target(blst_lib ALL DEPENDS ${BLST_LIB})

# Declare the blst library as an imported target
add_library(blst STATIC IMPORTED GLOBAL)
set_target_properties(blst PROPERTIES IMPORTED_LOCATION ${BLST_LIB})



# Example of linking the blst library to another target
# add_executable(my_executable main.c)
# target_link_libraries(my_executable PR‚IVATE blst)
