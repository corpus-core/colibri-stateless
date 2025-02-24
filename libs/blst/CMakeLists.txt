include(FetchContent)

# Define the output path for the static library
set(BLST_LIB_OUTPUT_DIR ${CMAKE_BINARY_DIR}/libs/blst)

if(WIN32)
    set(BLST_LIB ${BLST_LIB_OUTPUT_DIR}/blst.lib)
else()
    set(BLST_LIB ${BLST_LIB_OUTPUT_DIR}/libblst.a)
endif()

# Ensure the output directory exists
file(MAKE_DIRECTORY ${BLST_LIB_OUTPUT_DIR})

# Fetch the blst repository
FetchContent_Declare(
    blst
    GIT_REPOSITORY https://github.com/supranational/blst
    GIT_TAG v0.3.13
)

FetchContent_MakeAvailable(blst)
if(KOTLIN)
  set(EXTRA_C_FLAGS "-fPIC\ ")
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_UPPER)
if(CMAKE_BUILD_TYPE_UPPER STREQUAL "MINSIZEREL")
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
if(EMSCRIPTEN)
    set(EMCC emcc)

    # Add a custom command to build the static library for WASM
    add_custom_command(
        OUTPUT ${BLST_LIB}
        COMMAND ${EMCC} -I.. -fexceptions -include stddef.h
                -sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=[$intArrayFromString]
                -o ${BLST_LIB}
                -c ${blst_SOURCE_DIR}/src/server.c
        WORKING_DIRECTORY ${blst_SOURCE_DIR}
        COMMENT "Building blst as a static library for WebAssembly with Emscripten"
        VERBATIM
    )

elseif(WIN32)
    add_custom_command(
        OUTPUT ${BLST_LIB}
        COMMAND ${CMAKE_COMMAND} -E env CFLAGS=${EXTRA_C_FLAGS}-ffunction-sections\ -fdata-sections ${blst_SOURCE_DIR}/build.bat -D__BLST_PORTABLE__
        COMMAND ${CMAKE_COMMAND} -E echo "Checking if blst.lib exists"
        COMMAND ${CMAKE_COMMAND} -E copy ${blst_SOURCE_DIR}/blst.lib ${BLST_LIB}
        WORKING_DIRECTORY ${blst_SOURCE_DIR}
        COMMENT "Building and copying blst.lib with optimization flags on Windows"
        VERBATIM
    )
else()
    # For cross-compilation, we need to pass the appropriate toolchain
    if(CMAKE_CROSSCOMPILING)
        add_custom_command(
            OUTPUT ${BLST_LIB}
            COMMAND ${CMAKE_COMMAND} -E env 
                CC=${CMAKE_C_COMPILER}
                AR=${CMAKE_AR}
                RANLIB=${CMAKE_RANLIB}
                CFLAGS=${EXTRA_C_FLAGS}-ffunction-sections\ -fdata-sections
                bash ${blst_SOURCE_DIR}/build.sh -D__BLST_PORTABLE__
            COMMAND ${CMAKE_COMMAND} -E rename ${blst_SOURCE_DIR}/libblst.a ${BLST_LIB}
            WORKING_DIRECTORY ${blst_SOURCE_DIR}
            COMMENT "Building and moving libblst.a with cross-compilation toolchain"
            VERBATIM
        )
    else()
        add_custom_command(
            OUTPUT ${BLST_LIB}
            COMMAND ${CMAKE_COMMAND} -E env 
                CFLAGS=${EXTRA_C_FLAGS}-ffunction-sections\ -fdata-sections 
                bash ${blst_SOURCE_DIR}/build.sh -D__BLST_PORTABLE__
            COMMAND ${CMAKE_COMMAND} -E rename ${blst_SOURCE_DIR}/libblst.a ${BLST_LIB}
            WORKING_DIRECTORY ${blst_SOURCE_DIR}
            COMMENT "Building and moving libblst.a with optimization flags on non-Windows"
            VERBATIM
        )
    endif()
endif()

# Add a custom target that depends on the custom command
add_custom_target(blst_lib ALL DEPENDS ${BLST_LIB})

# Declare the blst library as an imported target
add_library(blst STATIC IMPORTED GLOBAL)
set_target_properties(blst PROPERTIES IMPORTED_LOCATION ${BLST_LIB})

add_dependencies(blst blst_lib)
