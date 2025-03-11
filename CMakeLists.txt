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
if (PROOFER) 
    add_definitions(-DPROOFER)
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

# Disable warnings about implicit void* casting
if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID MATCHES "GNU")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-incompatible-pointer-types")
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

# Combined static library target
if(NOT EMBEDDED)
    # Add an option to enable/disable combined static library
    option(COMBINED_STATIC_LIB "Build a combined static library" OFF)
    
    if(COMBINED_STATIC_LIB)
        # Helper function to recursively collect all static library dependencies
        function(get_all_library_dependencies target result_var dependency_set)
            # Get the target's dependencies
            get_target_property(target_deps ${target} LINK_LIBRARIES)
            if(target_deps)
                foreach(dep ${target_deps})
                    # Only process if it's a CMake target (not a path or keyword like optimized/debug)
                    if(TARGET ${dep})
                        # Get the type of the dependency
                        get_target_property(dep_type ${dep} TYPE)
                        
                        # Only include STATIC_LIBRARY targets and skip if already processed
                        if(dep_type STREQUAL "STATIC_LIBRARY" AND NOT ${dep} IN_LIST ${dependency_set})
                            # Add the dependency to our set to avoid duplicates
                            set(${dependency_set} ${${dependency_set}} ${dep} PARENT_SCOPE)
                            
                            # Recursively get this dependency's dependencies
                            get_all_library_dependencies(${dep} ${result_var} ${dependency_set})
                        endif()
                    endif()
                endforeach()
            endif()
            
            # Pass the result back to parent scope
            set(${result_var} ${${dependency_set}} PARENT_SCOPE)
        endfunction()
        
        # Initialize the list of libraries to combine
        set(ALL_LIBS_TO_COMBINE "")
        set(LIBS_DEPENDENCY_SET "")
        
        # Start with main libraries based on build options
        if(VERIFIER)
            list(APPEND ALL_LIBS_TO_COMBINE verifier)
            get_all_library_dependencies(verifier ALL_LIBS_TO_COMBINE LIBS_DEPENDENCY_SET)
        endif()
        
        if(PROOFER)
            list(APPEND ALL_LIBS_TO_COMBINE proofer)
            get_all_library_dependencies(proofer ALL_LIBS_TO_COMBINE LIBS_DEPENDENCY_SET)
        endif()
        
        # Remove duplicates
        list(REMOVE_DUPLICATES ALL_LIBS_TO_COMBINE)
        
        message(STATUS "Libraries to combine: ${ALL_LIBS_TO_COMBINE}")
        
        # Create the combined static library
        add_library(c4_combined STATIC)
        
        # Get all object files from the target libraries
        foreach(lib ${ALL_LIBS_TO_COMBINE})
            # Get sources, include dirs and compile definitions
            get_target_property(lib_sources ${lib} SOURCES)
            get_target_property(lib_include_dirs ${lib} INCLUDE_DIRECTORIES)
            get_target_property(lib_compile_defs ${lib} COMPILE_DEFINITIONS)
            
            # If include directories exist, add them to the combined library
            if(lib_include_dirs)
                target_include_directories(c4_combined PRIVATE ${lib_include_dirs})
            endif()
            
            # If compile definitions exist, add them to the combined library
            if(lib_compile_defs)
                target_compile_definitions(c4_combined PRIVATE ${lib_compile_defs})
            endif()
            
            # Add the sources to the combined library (using object library approach)
            if(lib_sources)
                target_sources(c4_combined PRIVATE $<TARGET_OBJECTS:${lib}>)
            endif()
        endforeach()
        
        # Set output properties for the combined library
        set_target_properties(c4_combined PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIRECTORY}
            OUTPUT_NAME "c4"
        )
        
        # Alternative approach using ar for combining libraries
        # Create a string with all the library file paths
        set(AR_LIBRARY_FILES "")
        foreach(lib ${ALL_LIBS_TO_COMBINE})
            set(AR_LIBRARY_FILES "${AR_LIBRARY_FILES} $<TARGET_FILE:${lib}>")
        endforeach()
        
        # Add a custom command to create the combined library using ar
        add_custom_command(
            OUTPUT ${OUTPUT_DIRECTORY}/libc4_combined.a
            COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIRECTORY}
            COMMAND ${CMAKE_AR} crs ${OUTPUT_DIRECTORY}/libc4_combined.a ${AR_LIBRARY_FILES}
            DEPENDS ${ALL_LIBS_TO_COMBINE}
            COMMENT "Creating combined static library using ar"
        )
        
        # Add a custom target for the combined library
        add_custom_target(c4_combined_lib ALL DEPENDS ${OUTPUT_DIRECTORY}/libc4_combined.a)
        
        # Install the combined library
        install(TARGETS c4_combined
            ARCHIVE DESTINATION lib
            LIBRARY DESTINATION lib
        )
    endif()
endif()

