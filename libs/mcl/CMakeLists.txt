cmake_minimum_required(VERSION 3.14)

include(FetchContent)

FetchContent_Declare(
  mcl
  GIT_REPOSITORY https://github.com/herumi/mcl.git
  GIT_TAG        v1.93
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
set(MCL_USE_OPENSSL OFF CACHE BOOL "Use OpenSSL" FORCE)

# 5. Disable Assembly/JIT (Fixes Windows ML64 error and Linux CI SIGILL/Illegal Instruction)
# This forces the use of portable C/C++ implementation.
set(MCL_USE_ASM OFF CACHE BOOL "Use Assembly" FORCE)
set(MCL_USE_LLVM OFF CACHE BOOL "Use LLVM IR" FORCE)

FetchContent_MakeAvailable(mcl)

# Create an interface library for easy linking
add_library(mcl_interface INTERFACE)
# Link against mcl::mcl_st (core static lib) and mcl::mclbn256 (BN254/BN256 wrapper)
target_link_libraries(mcl_interface INTERFACE mcl::mclbn256 mcl::mcl_st)
target_include_directories(mcl_interface INTERFACE ${mcl_SOURCE_DIR}/include)

# For compatibility with other parts of the project that might look for variables
set(MCL_INCLUDE_DIR ${mcl_SOURCE_DIR}/include PARENT_SCOPE)
# mclbn256 links against mcl_st internally, so we can just expose mclbn256
set(MCL_LIBRARY mcl::mclbn256 PARENT_SCOPE)
