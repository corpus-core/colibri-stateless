include(FetchContent)

# Define the output path for the static library
set(BLST_LIB_OUTPUT_DIR ${CMAKE_BINARY_DIR}/libs/blst)
set(BLST_LIB ${BLST_LIB_OUTPUT_DIR}/libblst.a)

# Ensure the output directory exists
file(MAKE_DIRECTORY ${BLST_LIB_OUTPUT_DIR})

# Fetch the blst repository
FetchContent_Declare(
    blst
    GIT_REPOSITORY https://github.com/supranational/blst
    GIT_TAG v0.3.13
)

FetchContent_MakeAvailable(blst)


# Add a custom command to build the library using the script
add_custom_command(
    OUTPUT ${BLST_LIB}
    COMMAND ${CMAKE_COMMAND} -E env CROSS_COMPILE=${CROSS_COMPILE} bash ${blst_SOURCE_DIR}/build.sh
    COMMAND ${CMAKE_COMMAND} -E rename ${blst_SOURCE_DIR}/libblst.a ${BLST_LIB}
    WORKING_DIRECTORY ${blst_SOURCE_DIR}
    COMMENT "Building and moving libblst.a"
    VERBATIM
)


# Add a custom target that depends on the custom command
add_custom_target(blst_lib ALL DEPENDS ${BLST_LIB})

# Declare the blst library as an imported target
add_library(blst STATIC IMPORTED GLOBAL)
set_target_properties(blst PROPERTIES IMPORTED_LOCATION ${BLST_LIB})

add_dependencies(blst blst_lib)

