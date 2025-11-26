cmake_minimum_required(VERSION 3.14)

include(FetchContent)

FetchContent_Declare(
  mcl
  GIT_REPOSITORY https://github.com/herumi/mcl.git
  GIT_TAG        v1.93
)

# Options for MCL
set(MCL_STATIC_LIB ON CACHE BOOL "Build static library" FORCE)
set(MCL_BUILD_TESTING OFF CACHE BOOL "Build tests" FORCE)
set(MCL_BUILD_SAMPLE OFF CACHE BOOL "Build samples" FORCE)

# Optimize for BN254 (ZK Proofs) - Disables support for larger curves like BLS12-381
# inside MCL, which reduces binary size significantly.
# Note: We use 'blst' for standard Ethereum BLS12-381 operations elsewhere.
set(MCL_MAX_BIT_SIZE 256 CACHE STRING "Max bit size (256 for BN254)" FORCE)

# For WASM/Embedded, we might want to disable some optimizations if they cause issues,
# but MCL is generally very portable.
if(EMSCRIPTEN)
    set(MCL_USE_LLVM OFF CACHE BOOL "Use LLVM IR" FORCE)
endif()

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
