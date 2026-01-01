cmake_minimum_required(VERSION 3.14)

include(FetchContent)

FetchContent_Declare(
  mcl
  GIT_REPOSITORY https://github.com/herumi/mcl.git
  GIT_TAG        v3.04
)

# Options for MCL - Optimized for Portability and CI Stability

# 1. Force Static Lib - Important for Emscripten/WASM and iOS
set(MCL_STATIC_LIB ON CACHE BOOL "Build static library" FORCE)

# Ensure we never build MCL as shared just because the parent project builds a shared library.
# This avoids ABI/runtime mismatches (notably on Windows) and keeps linking deterministic.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
set(MCL_SHARED_LIB OFF CACHE BOOL "Build shared library" FORCE)

# 2. Disable Bloat
set(MCL_BUILD_TESTING OFF CACHE BOOL "Build tests" FORCE)
set(MCL_BUILD_SAMPLE OFF CACHE BOOL "Build samples" FORCE)

# 3. Optimize for BN254 (ZK Proofs)
set(MCL_MAX_BIT_SIZE 256 CACHE STRING "Max bit size (256 for BN254)" FORCE)

# 4. Disable External Dependencies (Fixes WASM/Emscripten GMP error)
set(MCL_USE_GMP OFF CACHE BOOL "Use GMP" FORCE)
set(MCL_USE_GMP OFF) # Set as normal variable too, to override option() check if necessary
set(MCL_TEST_WITH_GMP OFF CACHE BOOL "Use GMP for tests" FORCE)
set(MCL_TEST_WITH_GMP OFF)
set(MCL_USE_OPENSSL OFF CACHE BOOL "Use OpenSSL" FORCE)
set(MCL_USE_OPENSSL OFF)

# Define only flags for missing dependencies
add_definitions(-DMCL_DONT_USE_OPENSSL)

FetchContent_MakeAvailable(mcl)

# Create an interface library for easy linking
add_library(mcl_interface INTERFACE)

# Link against mcl::mcl_st (core static lib)
# We use the target if available, which handles paths correctly.
if(TARGET mcl::mcl_st)
    target_link_libraries(mcl_interface INTERFACE mcl::mcl_st)
elseif(TARGET mcl_st)
    target_link_libraries(mcl_interface INTERFACE mcl_st)
else()
    message(WARNING "MCL: Target mcl::mcl_st not found. Linking might fail.")
endif()

# Link against mclbn256 (BN254 support)
# We prefer the target (mcl::mclbn256 or mclbn256) to handle build-time dependencies.
if(TARGET mcl::mclbn256)
    target_link_libraries(mcl_interface INTERFACE mcl::mclbn256)
elseif(TARGET mclbn256)
    target_link_libraries(mcl_interface INTERFACE mclbn256)
else()
    # Fallback: Target mclbn256 missing (likely due to mcl build config on this platform).
    # Build it manually from source to ensure it exists and is linked correctly.
    message(STATUS "MCL: Target mclbn256 not found. Building manually from source.")
    
    add_library(mclbn256_manual STATIC "${mcl_SOURCE_DIR}/src/bn_c256.cpp")
    
    # Ensure it has access to mcl headers and internal sources if needed
    target_include_directories(mclbn256_manual PRIVATE "${mcl_SOURCE_DIR}/include" "${mcl_SOURCE_DIR}/src")
    
    # Defines
    target_compile_definitions(mclbn256_manual PRIVATE MCL_NO_AUTOLINK MCLBN_NO_AUTOLINK)
    
    # Link against mcl core (which defines necessary flags like MCL_FP_BIT)
    if(TARGET mcl::mcl_st)
        target_link_libraries(mclbn256_manual PRIVATE mcl::mcl_st)
    elseif(TARGET mcl_st)
        target_link_libraries(mclbn256_manual PRIVATE mcl_st)
    endif()
    
    target_link_libraries(mcl_interface INTERFACE mclbn256_manual)
endif()

target_include_directories(mcl_interface INTERFACE ${mcl_SOURCE_DIR}/include)

# Ensure consumers know about dependency configuration
target_compile_definitions(mcl_interface INTERFACE MCL_DONT_USE_OPENSSL)


# For compatibility with other parts of the project that might look for variables
set(MCL_INCLUDE_DIR ${mcl_SOURCE_DIR}/include PARENT_SCOPE)
set(MCL_LIBRARY mcl_interface PARENT_SCOPE)
