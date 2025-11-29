cmake_minimum_required(VERSION 3.14)

include(FetchContent)

FetchContent_Declare(
  mcl
  GIT_REPOSITORY https://github.com/herumi/mcl.git
  GIT_TAG        v3.04
)

# Options for MCL - Optimized for Portability and CI Stability

# 1. Force Static Lib
set(MCL_STATIC_LIB ON CACHE BOOL "Build static library" FORCE)

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
# mclbn256 is tricky because it's not always exported as a target in the way we expect.
# We find the library file manually to be safe.
target_link_libraries(mcl_interface INTERFACE mcl::mcl_st)

find_library(LIB_MCLBN256 NAMES mclbn256 mclbn256_static 
             PATHS ${mcl_BINARY_DIR}/lib ${CMAKE_BINARY_DIR}/lib 
             NO_DEFAULT_PATH)

if(LIB_MCLBN256)
    target_link_libraries(mcl_interface INTERFACE ${LIB_MCLBN256})
else()
    # Try linking as target if find_library failed (fallback)
    target_link_libraries(mcl_interface INTERFACE mclbn256)
endif()

target_include_directories(mcl_interface INTERFACE ${mcl_SOURCE_DIR}/include)

# Ensure consumers know about dependency configuration
target_compile_definitions(mcl_interface INTERFACE MCL_DONT_USE_OPENSSL)


# For compatibility with other parts of the project that might look for variables
set(MCL_INCLUDE_DIR ${mcl_SOURCE_DIR}/include PARENT_SCOPE)
set(MCL_LIBRARY mcl_interface PARENT_SCOPE)
