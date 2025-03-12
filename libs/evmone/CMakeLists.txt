# Suppress Hunter CMake deprecation warnings
set(CMAKE_POLICY_DEFAULT_CMP0075 NEW)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
  cmake_policy(SET CMP0155 NEW)
endif()
# Set CMake to ignore deprecated warnings (this addresses the specific warning you're seeing)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.10)
  set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
endif()


# evmone integration for C projects - ExternalProject approach
#cmake_minimum_required(VERSION 3.16)


# C wrapper for evmone
set(EVMONE_C_WRAPPER_SOURCES 
    ${CMAKE_CURRENT_SOURCE_DIR}/evmone_c_wrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/evmone_c_wrapper.h
)

# For WASM builds, use a simplified implementation without external deps
if(EMSCRIPTEN OR WASM)
    # For WASM, create a special stub implementation
    set(WASM_STUB_FILE "${CMAKE_CURRENT_BINARY_DIR}/evmone_wasm_stub.cpp")
    file(WRITE ${WASM_STUB_FILE} "
// Simplified stub implementation for WASM builds that doesn't depend on external headers
#include <cstdint>
#include <cstdlib>
#include <cstring>

// Define the WASM build flag for the header
#define EVMONE_WASM_BUILD 1

// Include our wrapper header which now has conditional definitions
#include \"${CMAKE_CURRENT_SOURCE_DIR}/evmone_c_wrapper.h\"

// Forward declarations needed for our implementation
struct evmc_vm {
    // Just enough to satisfy the interface
    evmone_result (*execute)(struct evmc_vm*, const evmone_host_interface*, 
                        void*, int, 
                        const evmone_message*, const uint8_t*, size_t);
};

struct evmc_host_context;
struct evmc_host_interface;
struct evmc_message;

#ifdef __cplusplus
extern \"C\" {
#endif

// Simple stub implementation for execute
static evmone_result execute_stub(struct evmc_vm* vm, const evmone_host_interface* host, 
                        void* context, int rev, 
                        const evmone_message* msg, const uint8_t* code, size_t code_size) {
    evmone_result result = {};
    result.status_code = 0; // Success
    // Return empty result
    return result;
}

// Create EVM implementation - minimal for WASM
struct evmc_vm* evmc_create_evmone() {
    static evmc_vm vm = {};
    vm.execute = &execute_stub;
    return &vm;
}

// Alias - providing both symbols for compatibility
struct evmc_vm* evmone_create() {
    return evmc_create_evmone();
}

// Create EVM executor - direct implementation
void* evmone_create_executor() {
    return evmc_create_evmone();
}

// Destroy EVM executor - no-op in stub
void evmone_destroy_executor(void* executor) {
    // No-op in stub implementation
}

// Execute code in the EVM - simplified stub
evmone_result evmone_execute(
    void* executor,
    const evmone_host_interface* host_interface,
    void* host_context,
    int revision,
    const evmone_message* msg,
    const uint8_t* code,
    size_t code_size) {

    // Create minimal result
    evmone_result result = {};
    result.status_code = 0; // Success
    result.gas_left = 0;
    
    return result;
}

// Release result resources - no-op in stub
void evmone_release_result(evmone_result* result) {
    // Just clear the fields
    if (result) {
        result->output_data = nullptr;
        result->output_size = 0;
    }
}

#ifdef __cplusplus
}
#endif
")

    # Also create a special C++ wrapper for WASM
    set(WASM_WRAPPER_FILE "${CMAKE_CURRENT_BINARY_DIR}/evmone_wasm_wrapper.cpp")
    file(WRITE ${WASM_WRAPPER_FILE} "
/**
 * C wrapper implementation for evmone library - WASM stub version
 */
#include \"${CMAKE_CURRENT_SOURCE_DIR}/evmone_c_wrapper.h\"

// Define EVMONE_WASM_BUILD to ensure header knows we're in WASM mode
#ifndef EVMONE_WASM_BUILD
#define EVMONE_WASM_BUILD 1
#endif

#ifdef __cplusplus
extern \"C\" {
#endif

// Forward declare our functions that are implemented in the stub
struct evmc_vm* evmc_create_evmone();
struct evmc_vm* evmone_create();

#ifdef __cplusplus
}
#endif

// Empty file - all implementations are in the stub
")

    # Use the stub implementation directly for WASM
    set(EVMONE_C_WRAPPER_SOURCES 
        ${CMAKE_CURRENT_SOURCE_DIR}/evmone_c_wrapper.h
        ${WASM_WRAPPER_FILE}
        ${WASM_STUB_FILE}
    )
endif()

# Create our main evmone wrapper target
add_library(evmone_wrapper STATIC ${EVMONE_C_WRAPPER_SOURCES})

# Set C++ standard - evmone uses C++17 features
set_target_properties(evmone_wrapper PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Add required compile definitions for WASM if needed
if(EMSCRIPTEN OR WASM)
    target_compile_definitions(evmone_wrapper PRIVATE EVMONE_WASM_BUILD=1)
    target_compile_definitions(evmone_wrapper PUBLIC EVMONE_WASM_BUILD=1)
    
    # Apply Emscripten-specific flags when using Emscripten
    if(EMSCRIPTEN)
        # For WASM, we're not using the external project at all
        message(STATUS "Using simplified evmone stub implementation for WASM")
        
        # Add Emscripten-specific flags for export and memory
        set_target_properties(evmone_wrapper PROPERTIES
            LINK_FLAGS "-sEXPORT_ALL=1 -sALLOW_MEMORY_GROWTH=1"
        )
    endif()
else()
    # Setup external build for evmone - only for native builds
    message(STATUS "Setting up external build for evmone")

    # External project for building evmone
    include(ExternalProject)

    # Determine build type flags
    set(CMAKE_BUILD_TYPE_FLAGS "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")

    # Define paths for built libraries and source directories
    set(EVMONE_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/evmone/src/evmone_external")
    set(EVMONE_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/evmone/src/evmone_external-build")
    set(EVMONE_BUILT_LIB "${EVMONE_BINARY_DIR}/lib/libevmone-standalone.a")
    set(EVMONE_PRECOMPILES_BUILT_LIB "${EVMONE_BINARY_DIR}/lib/evmone_precompiles/libevmone_precompiles.a")

    # Also add EVMC libraries
    set(EVMC_INSTRUCTIONS_LIB "${EVMONE_BINARY_DIR}/evmc/lib/instructions/libevmc-instructions.a")
    set(EVMC_TOOLING_LIB "${EVMONE_BINARY_DIR}/evmc/lib/tooling/libtooling.a")
    set(EVMC_LOADER_LIB "${EVMONE_BINARY_DIR}/evmc/lib/loader/libevmc-loader.a")

    # Set compiler flags
    set(COMPILER_FLAGS
        "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
        "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DCMAKE_LINKER=${CMAKE_LINKER}"
        "-DCMAKE_AR=${CMAKE_AR}"
        "-DCMAKE_RANLIB=${CMAKE_RANLIB}"
        "-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}"
        "-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
        "-DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
        "-DCMAKE_SHARED_LINKER_FLAGS=${CMAKE_SHARED_LINKER_FLAGS}"
        "-DCMAKE_MODULE_LINKER_FLAGS=${CMAKE_MODULE_LINKER_FLAGS}"
        "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
    )

    # The actual external project definition
    ExternalProject_Add(
        evmone_external
        PREFIX "${CMAKE_CURRENT_BINARY_DIR}/evmone"
        GIT_REPOSITORY https://github.com/corpus-core/evmone.git
        GIT_SHALLOW TRUE
        CMAKE_ARGS
            ${CMAKE_BUILD_TYPE_FLAGS}
            ${COMPILER_FLAGS}
            -DBUILD_SHARED_LIBS=OFF
            -DEVMONE_TESTING=OFF
            -DCMAKE_CXX_STANDARD=17
            # Pass warning suppression flags to the external build
            -DCMAKE_WARN_DEPRECATED=OFF
            -DCMAKE_POLICY_DEFAULT_CMP0075=NEW
        # Skip install step as we'll link directly
        INSTALL_COMMAND ""
    )

    # Set include directories for the wrapper
    target_include_directories(evmone_wrapper 
        PRIVATE
            # Main evmone includes
            "${EVMONE_SOURCE_DIR}/include"
            "${EVMONE_SOURCE_DIR}/lib"
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}  # For the wrapper header
            # EVMC includes - moved to PUBLIC so they're propagated to targets that use our header
            "${EVMONE_SOURCE_DIR}/evmc/include"
            "${EVMONE_BINARY_DIR}/evmc/include"
    )

    # Link directly to the built libraries
    target_link_libraries(evmone_wrapper PRIVATE
        "${EVMONE_BUILT_LIB}"
        "${EVMONE_PRECOMPILES_BUILT_LIB}"
        # Also link against EVMC libraries
        "${EVMC_INSTRUCTIONS_LIB}"
        "${EVMC_TOOLING_LIB}"
        "${EVMC_LOADER_LIB}"
    )

    # Add a dependency to ensure evmone is built before our wrapper
    add_dependencies(evmone_wrapper evmone_external)
endif()

# Include directories for all builds
target_include_directories(evmone_wrapper PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Create alias for convenience
add_library(evmone ALIAS evmone_wrapper)

# Install targets
install(TARGETS evmone_wrapper
        EXPORT evmone-targets
        ARCHIVE DESTINATION lib
        INCLUDES DESTINATION include)

install(FILES evmone_c_wrapper.h
        DESTINATION include)

# ======================= BUILD INSTRUCTIONS =======================
# This CMakeLists.txt builds evmone as an external project as part of your build process.
#
# To use:
# 1. Add this directory to your project with add_subdirectory(libs/evmone)
# 2. Link against the 'evmone' target in your own targets
#
# For WASM builds, set -DEVMONE_WASM_BUILD=ON when running cmake
# Example: cmake -DEVMONE_WASM_BUILD=ON ..
# ==================================================================== 