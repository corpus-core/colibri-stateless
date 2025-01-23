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

if(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    # Compiler-Flags für Größe-Optimierung
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections -Os -g0")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections -Os -g0")

    # Unterscheide zwischen Plattformen
    if(APPLE)
        # macOS-spezifische Flags
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -dead_strip")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -dead_strip")
    else()
        # Standard (z. B. für Linux)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --gc-sections")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} --gc-sections")
    endif()
    # Add a custom command to build the library using the script
endif()

add_custom_command(
    OUTPUT ${BLST_LIB}
    COMMAND ${CMAKE_COMMAND} -E env CFLAGS=-ffunction-sections\ -fdata-sections bash ${blst_SOURCE_DIR}/build.sh -D__BLST_PORTABLE__
    COMMAND ${CMAKE_COMMAND} -E rename ${blst_SOURCE_DIR}/libblst.a ${BLST_LIB}
    WORKING_DIRECTORY ${blst_SOURCE_DIR}
    COMMENT "Building and moving libblst.a with optimization flags"
    VERBATIM
)

# Add a custom target that depends on the custom command
add_custom_target(blst_lib ALL DEPENDS ${BLST_LIB})

# Declare the blst library as an imported target
add_library(blst STATIC IMPORTED GLOBAL)
set_target_properties(blst PROPERTIES IMPORTED_LOCATION ${BLST_LIB})

add_dependencies(blst blst_lib)