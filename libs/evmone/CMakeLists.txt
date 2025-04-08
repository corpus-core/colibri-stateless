# Use ExternalProject for evmone - no Hunter initialization
include(FetchContent)

# Ensure CMake allows FetchContent_Populate() if policy exists
# CMP0169 was introduced in CMake 3.27
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()

# set the compiler options
if(MSVC)
    if(CMAKE_CXX_COMPILER_ID MATCHES Clang) # clang-cl
        add_compile_options(
            -Wno-exit-time-destructors
            -Wno-global-constructors
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-c++98-compat-pedantic>
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-old-style-cast>
        )
    else()
        # MSVC-specific warning suppressions
        add_compile_options(
            /wd4324  # structure was padded due to alignment specifier
            /wd4245  # conversion from 'type1' to 'type2', signed/unsigned mismatch
            /wd4146  # unary minus operator applied to unsigned type, result still unsigned
        )
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES Clang)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Og")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Og")
    set(CMAKE_C_FLAGS_COVERAGE "${CMAKE_C_FLAGS_DEBUG} -fprofile-instr-generate -fcoverage-mapping")
    set(CMAKE_CXX_FLAGS_COVERAGE "${CMAKE_CXX_FLAGS_COVERAGE} -fprofile-instr-generate -fcoverage-mapping")
elseif(CMAKE_CXX_COMPILER_ID MATCHES GNU)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Og")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Og")
    set(CMAKE_C_FLAGS_COVERAGE "${CMAKE_C_FLAGS_DEBUG} --coverage")
    set(CMAKE_CXX_FLAGS_COVERAGE "${CMAKE_CXX_FLAGS_DEBUG} --coverage")
endif()

if(CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64)
    # Setup options for x86_64 micro-architecture levels.
    # https://clang.llvm.org/docs/UsersManual.html#x86

    set(EVMONE_X86_64_ARCH_LEVEL_INIT 2)
    if(APPLE)
        # On macos with Apple Silicon CPU (arm64) the x86 is emulated and SSE4.2 is not available.
        set(EVMONE_X86_64_ARCH_LEVEL_INIT 1)
    endif()

    set(EVMONE_X86_64_ARCH_LEVEL ${EVMONE_X86_64_ARCH_LEVEL_INIT} CACHE STRING "The x86_64 micro-architecture level")
    if(EVMONE_X86_64_ARCH_LEVEL GREATER_EQUAL 1 AND EVMONE_X86_64_ARCH_LEVEL LESS_EQUAL 4)
        message(STATUS "x86_64 micro-architecture level: ${EVMONE_X86_64_ARCH_LEVEL}")
        if(EVMONE_X86_64_ARCH_LEVEL GREATER_EQUAL 2)
            add_compile_options(-march=x86-64-v${EVMONE_X86_64_ARCH_LEVEL})
        endif()
    else()
        message(FATAL_ERROR "Invalid EVMONE_X86_64_ARCH_LEVEL: ${EVMONE_X86_64_ARCH_LEVEL}")
    endif()
endif()



# include keccak
FetchContent_Declare(
    ethhash_external
    GIT_REPOSITORY https://github.com/chfast/ethash.git
    GIT_SHALLOW TRUE
)

FetchContent_GetProperties(ethhash_external)
if(NOT ethhash_external_POPULATED)
    FetchContent_Populate(ethhash_external)
    # Eigene CMakeLists.txt anlegen
    file(WRITE ${ethhash_external_SOURCE_DIR}/CMakeLists.txt "
        # Set deployment target for iOS builds
        if(APPLE)
            set(CMAKE_OSX_DEPLOYMENT_TARGET \"${CMAKE_OSX_DEPLOYMENT_TARGET}\" CACHE STRING \"Minimum deployment target\" FORCE)
        endif()

        add_library(keccak STATIC
            lib/keccak/keccak.c
        )
        target_include_directories(keccak PUBLIC include)
    ")
endif()
add_subdirectory(${ethhash_external_SOURCE_DIR} ${ethhash_external_BINARY_DIR} EXCLUDE_FROM_ALL)




# include evmone
FetchContent_Declare(
    evmone_external
    GIT_REPOSITORY https://github.com/ethereum/evmone.git
    GIT_SHALLOW TRUE
    RECURSE TRUE
)

FetchContent_GetProperties(evmone_external)
if(NOT evmone_external_POPULATED)
    FetchContent_Populate(evmone_external)
    
    # Copy compatibility files to the source directory
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/compat/aligned_alloc_fallback.hpp 
         DESTINATION ${evmone_external_SOURCE_DIR}/lib/evmone/)
    
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/compat/ios_compat.hpp 
         DESTINATION ${evmone_external_SOURCE_DIR}/evmc/include/evmc/)
    
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/compat/cpp20_compat.hpp 
         DESTINATION ${evmone_external_SOURCE_DIR}/lib/evmone/)
    
    # Apply patches using the CMake scripts
    set(SOURCE_DIR ${evmone_external_SOURCE_DIR})
    include(${CMAKE_CURRENT_SOURCE_DIR}/compat/patch_execution_state.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/compat/patch_evmc.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/compat/patch_eof.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/compat/patch_instructions_calls.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/compat/patch_baseline_analysis.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/compat/patch_consteval.cmake)
    
    # Now create the CMakeLists.txt file
    file(WRITE ${evmone_external_SOURCE_DIR}/CMakeLists.txt "
        # Set deployment target for iOS builds
        if(APPLE)
            set(CMAKE_OSX_DEPLOYMENT_TARGET \"${CMAKE_OSX_DEPLOYMENT_TARGET}\" CACHE STRING \"Minimum deployment target\" FORCE)
            # Enable aligned_alloc fallback for iOS
            add_definitions(-DUSE_ALIGNED_ALLOC_FALLBACK)
        endif()

        # Windows-specific settings
        if(MSVC OR WIN32)
            add_definitions(-DUSE_ALIGNED_ALLOC_FALLBACK)
        endif()

        add_definitions(-DPROJECT_VERSION=\\\"0.14.1\\\")

        add_library(evmone STATIC
            include/evmone/evmone.h
            lib/evmone/advanced_analysis.cpp
            lib/evmone/advanced_analysis.hpp
            lib/evmone/advanced_execution.cpp
            lib/evmone/advanced_execution.hpp
            lib/evmone/advanced_instructions.cpp
            lib/evmone/baseline.hpp
            lib/evmone/baseline_analysis.cpp
            lib/evmone/baseline_execution.cpp
            lib/evmone/baseline_instruction_table.cpp
            lib/evmone/baseline_instruction_table.hpp
            lib/evmone/constants.hpp
            lib/evmone/delegation.cpp
            lib/evmone/delegation.hpp
            lib/evmone/eof.cpp
            lib/evmone/eof.hpp
            lib/evmone/instructions.hpp
            lib/evmone/instructions_calls.cpp
            lib/evmone/instructions_opcodes.hpp
            lib/evmone/instructions_storage.cpp
            lib/evmone/instructions_traits.hpp
            lib/evmone/instructions_xmacro.hpp
            lib/evmone/tracing.cpp
            lib/evmone/tracing.hpp
            lib/evmone/vm.cpp
            lib/evmone/vm.hpp        
        )
        target_include_directories(evmone PUBLIC include evmc/include)
        target_compile_features(evmone PUBLIC cxx_std_20)
        target_link_libraries(evmone PUBLIC intx::intx PRIVATE keccak)

        if(EVMONE_X86_64_ARCH_LEVEL GREATER_EQUAL 2)
            # Add CPU architecture runtime check. The EVMONE_X86_64_ARCH_LEVEL has a valid value.
            target_sources(evmone PRIVATE lib/evmone/cpu_check.cpp)
            set_source_files_properties(lib/evmone/cpu_check.cpp PROPERTIES COMPILE_DEFINITIONS EVMONE_X86_64_ARCH_LEVEL=${EVMONE_X86_64_ARCH_LEVEL})
        endif()

        # Suppress specific warnings for evmone target if using Clang or GCC
        if(CMAKE_CXX_COMPILER_ID MATCHES \"Clang|GNU\")
            target_compile_options(evmone PUBLIC 
                -Wno-deprecated-literal-operator
                -Wno-unknown-attributes
            )
        endif()
    ")
endif()
add_subdirectory(${evmone_external_SOURCE_DIR} ${evmone_external_BINARY_DIR} EXCLUDE_FROM_ALL)


# Create our wrapper library
add_library(evmone_wrapper STATIC  EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/evmone_c_wrapper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/evmone_c_wrapper.h
)

# Set properties
set_target_properties(evmone_wrapper PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Add include directories - make EVMC includes PUBLIC so they propagate
target_include_directories(evmone_wrapper 
    PRIVATE
        ${evmone_external_SOURCE_DIR}/include       # evmone private headers
    PUBLIC
        ${evmone_external_SOURCE_DIR}/evmc/include  # EVMC headers needed by clients
        ${CMAKE_CURRENT_SOURCE_DIR} # Our wrapper header
)

target_link_libraries(evmone_wrapper PUBLIC evmone keccak)

# Install targets
install(TARGETS evmone_wrapper
    ARCHIVE DESTINATION lib
    INCLUDES DESTINATION include
)

install(FILES evmone_c_wrapper.h
    DESTINATION include
)

