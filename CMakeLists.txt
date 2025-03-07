cmake_minimum_required(VERSION 3.10...3.32)
project(c4)

# Core build options
option(CURL "Enable CURL support" ON)
option(PROOFER "Build the proofer library" ON)
option(CLI "Build command line tools" ON)
option(VERIFIER "Build the verifier library" ON)
option(BLS_DESERIALIZE "Store BLS keys deserialized (faster but more memory)" ON)
option(PROOFER_CACHE "Caches blockhashes and maps)" ON)
option(CHAIN_ETH "includes all ETH engine" ON)
# Build type options
option(EMBEDDED "Build for embedded target" OFF)
option(WASM "Build WebAssembly target" OFF)
option(KOTLIN "Build Kotlin bindings" OFF)
option(SWIFT "Build Swift bindings" OFF)
option(TEST "Build tests" OFF)
option(COVERAGE "Enable coverage" OFF)
option(SHAREDLIB "Build shared library" OFF)
option(STATIC_MEMORY "if true, the memory will be statically allocated (only for embedded systems)" OFF)

# Additional include path (for embedded targets etc)
set(INCLUDE "" CACHE STRING "Additional directory to include in build")

if(EMBEDDED)
    # Force embedded-friendly options
    set(CURL OFF)
    set(PROOFER OFF)
    set(CLI OFF)
    set(STATIC_MEMORY ON)
    set(BLS_DESERIALIZE OFF)
        
    # Force portable implementations
    add_definitions(-DBLST_PORTABLE -DEMBEDDED)
    set(BLS_SIGNATURES_BUILD_SHARED_LIBS OFF)
    set(BLS_SIGNATURES_USE_STATIC ON)
endif()

if(SWIFT)
    # Set SDK paths based on target architecture
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
        execute_process(
            COMMAND xcrun --sdk iphonesimulator --show-sdk-path
            OUTPUT_VARIABLE CMAKE_OSX_SYSROOT
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        set(IOS_PLATFORM "SIMULATOR64")
        set(TARGET_TRIPLE "x86_64-apple-ios-simulator")
    elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        execute_process(
            COMMAND xcrun --sdk iphoneos --show-sdk-path
            OUTPUT_VARIABLE CMAKE_OSX_SYSROOT
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        set(IOS_PLATFORM "OS64")
        set(TARGET_TRIPLE "arm64-apple-ios")
    endif()

    # Set common iOS build settings
    set(CMAKE_SYSTEM_NAME "iOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum iOS deployment version" FORCE)
    set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH NO)
    set(CMAKE_IOS_INSTALL_COMBINED NO)
    
    # Set iOS-specific compiler and linker flags
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -target ${TARGET_TRIPLE} -isysroot ${CMAKE_OSX_SYSROOT}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -target ${TARGET_TRIPLE} -isysroot ${CMAKE_OSX_SYSROOT}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -target ${TARGET_TRIPLE}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -target ${TARGET_TRIPLE}")
    
    # Disable CURL for iOS builds
    set(CURL OFF)
endif()
if(KOTLIN)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(CURL OFF)
endif()

# Feature flags and compiler settings
if (PROOFER_CACHE) 
    add_definitions(-DPROOFER_CACHE)
endif()
if(CURL)
    add_definitions(-DUSE_CURL)
endif()

if(STATIC_MEMORY)
    add_definitions(-DC4_STATIC_MEMORY)
endif()

if(BLS_DESERIALIZE)
    add_definitions(-DBLS_DESERIALIZE)
endif()

if(TEST)
    if(NOT DEFINED TESTDATA_DIR)
        set(TESTDATA_DIR "${CMAKE_SOURCE_DIR}/test/data")
    endif()
    add_definitions(-DTEST -DTESTDATA_DIR="${TESTDATA_DIR}")
endif()

if(COVERAGE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-arcs -ftest-coverage")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
endif()

# Output settings
set(OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Size optimization settings
string(TOUPPER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_UPPER)
if(CMAKE_BUILD_TYPE_UPPER STREQUAL "MINSIZEREL")
    if(POLICY CMP0069)
        cmake_policy(SET CMP0069 NEW)
    endif()
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

    if(MSVC)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /Gy")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Gy")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /OPT:REF /OPT:ICF")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /OPT:REF /OPT:ICF")
    else()
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")
        if(APPLE)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -dead_strip")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -dead_strip")
        else()
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections")
        endif()
    endif()
endif()

include(src/chains/chains.cmake)

# Define the path for the generated headers
add_definitions(-DVERIFIERS_PATH="${CMAKE_BINARY_DIR}/verifiers.h")
add_definitions(-DPROOFERS_PATH="${CMAKE_BINARY_DIR}/proofers.h")

# Add core libraries first
add_subdirectory(libs)
add_subdirectory(src)

# Additional includes (like embedded targets)
if(INCLUDE)
    add_subdirectory(${INCLUDE})
endif()

# Optional components
if((TEST) AND (NOT EMBEDDED))
    enable_testing()
    add_subdirectory(test)
    set(CMAKE_CTEST_ARGUMENTS "--output-on-failure")
endif()

if(WASM)
    add_subdirectory(bindings/emscripten)
endif()

if(KOTLIN)
    add_subdirectory(bindings/kotlin)
endif()

if(SWIFT)
    add_subdirectory(bindings/swift)
endif()

# Shared library if requested and not embedded
if(SHAREDLIB AND NOT EMBEDDED)
    add_library(c4Lib SHARED src/util/version.c)
    target_link_libraries(c4Lib PRIVATE crypto verifier util blst)
    set_target_properties(c4Lib PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIRECTORY}
        VERSION 1.0
        SOVERSION 1
    )
endif()

add_custom_target(doc 
    COMMAND ${CMAKE_COMMAND} -E echo "Generating documentation..."
    COMMAND ${CMAKE_COMMAND} -E env node ${CMAKE_SOURCE_DIR}/scripts/update_doc.js
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/scripts
    COMMENT "Running update_doc.js to generate documentation"
)

add_custom_target(valgrind
    COMMAND ${CMAKE_COMMAND} -E echo "Generating valgrind image..."
    COMMAND ${CMAKE_COMMAND} -E env bash -c "docker build -f test/valgrind/Dockerfile --platform=linux/amd64 -t c4_valgrind ."
    COMMAND ${CMAKE_COMMAND} -E env bash -c "docker run --rm --platform=linux/amd64 c4_valgrind"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating the valgrind image"
)
# Generate headers at the end
generate_verifiers_header()
generate_proofers_header()

