cmake_minimum_required(VERSION 3.10...3.32)
project(c4)

# Core build options
option(CURL "Enable CURL support" ON)
option(PROOFER "Build the proofer library" ON)
option(CLI "Build command line tools" ON)
option(VERIFIER "Build the verifier library" ON)
option(BLS_DESERIALIZE "Store BLS keys deserialized. It is faster but uses 25k more memory in cache per period." ON)
option(PROOFER_CACHE "Caches blockhashes and maps, which makes a lot of sense on a server" OFF)
option(CHAIN_ETH "includes the ETH verification support" ON)

# Build type options
option(EMBEDDED "Build for embedded target" OFF)
option(WASM "Build WebAssembly target" OFF)
option(KOTLIN "Build Kotlin bindings" OFF)
option(SWIFT "Build Swift bindings" OFF)
option(TEST "Build the unit tests" OFF)
option(COVERAGE "Enable coverage tracking" OFF)
option(SHAREDLIB "Build shared library" OFF)
option(STATIC_MEMORY "if true, the memory will be statically allocated, which only makes sense for embedded systems" OFF)
option(MESSAGES "if activated the binaries will contain error messages, but for embedded systems this is not needed and can be turned off to save memory" ON)
if(MESSAGES)
    add_definitions(-DMESSAGES)
endif()

# Additional include path (for embedded targets etc)
set(INCLUDE "" CACHE STRING "Additional directory to include in build")

# Add option for the HTTP server
option(HTTP_SERVER "Build the HTTP server using libuv and llhttp" OFF)
option(HTTP_SERVER_GEO "support for geo-location" ON)

if (HTTP_SERVER)
    add_definitions(-DHTTP_SERVER)
    if (HTTP_SERVER_GEO)
        add_definitions(-DHTTP_SERVER_GEO)
    endif()
    # Only add UV time definition if cache is also enabled, matching libuv link condition
    if (PROOFER_CACHE)
        add_definitions(-DC4_PROOFER_USE_UV_TIME)
    endif()
endif()


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

    # Set deployment target before any targets are created
    if(APPLE)
        # Set RPATH handling for macOS/iOS
        set(CMAKE_MACOSX_RPATH TRUE)

        if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64" OR 
        EXISTS "/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform" OR
        EXISTS "/Applications/Xcode_*.app/Contents/Developer/Platforms/iPhoneSimulator.platform" OR
        DEFINED IOS_SIMULATOR OR DEFINED IOS_PLATFORM)
            set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum iOS deployment target" FORCE)
            message(STATUS "Setting iOS deployment target to ${CMAKE_OSX_DEPLOYMENT_TARGET}")
            # For diagnostics, try to determine if this is iOS
            if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR DEFINED IOS_SIMULATOR OR DEFINED IOS_PLATFORM)
                message(STATUS "iOS platform detected: ${CMAKE_SYSTEM_NAME}")
            else()
                message(STATUS "Possible iOS build detected, but CMAKE_SYSTEM_NAME is: ${CMAKE_SYSTEM_NAME}")
            endif()
        elseif(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
            # For regular macOS x86_64 builds
            set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS deployment target" FORCE)
            message(STATUS "Setting macOS deployment target to ${CMAKE_OSX_DEPLOYMENT_TARGET}")
        endif()
    endif()


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
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections ")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")
        if(APPLE)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -dead_strip ")
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
    COMMAND ${CMAKE_COMMAND} -E env node ${CMAKE_SOURCE_DIR}/scripts/doc/index.js
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
            # Include the verifier target itself
            list(APPEND ALL_LIBS_TO_COMBINE verifier)
            get_all_library_dependencies(verifier LIBS_DEPENDENCY_SET LIBS_DEPENDENCY_SET)
        endif()
        
        if(PROOFER)
            # Include the proofer target itself
            list(APPEND ALL_LIBS_TO_COMBINE proofer)
            get_all_library_dependencies(proofer LIBS_DEPENDENCY_SET LIBS_DEPENDENCY_SET)
        endif()
        
        # Combine all discovered dependencies with the main targets
        list(APPEND ALL_LIBS_TO_COMBINE ${LIBS_DEPENDENCY_SET})
        
        # Remove duplicates
        list(REMOVE_DUPLICATES ALL_LIBS_TO_COMBINE)
        
        message(STATUS "Libraries to combine: ${ALL_LIBS_TO_COMBINE}")
        
        # Create a direct static library target instead of a custom command
        add_library(c4_combined STATIC "${CMAKE_CURRENT_BINARY_DIR}/dummy.c")
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/dummy.c" "/* Dummy file for combined library */\n")
        
        # Set output properties for the combined library
        set_target_properties(c4_combined PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIRECTORY}"
            OUTPUT_NAME "c4"
        )
        
        # Add dependencies to ensure libraries are built first
        foreach(lib ${ALL_LIBS_TO_COMBINE})
            add_dependencies(c4_combined ${lib})
        endforeach()
        
        # Create script header content at configure time
        set(SCRIPT_HEADER "#!/bin/bash
set -e
echo \"Creating combined library...\"
rm -rf \"${CMAKE_BINARY_DIR}/combine_work\"
mkdir -p \"${CMAKE_BINARY_DIR}/combine_work\"
mkdir -p \"${OUTPUT_DIRECTORY}\"
rm -f \"${OUTPUT_DIRECTORY}/libc4.a\"

# Create an empty archive with a dummy file
echo \"/* Dummy object */\" > \"${CMAKE_BINARY_DIR}/dummy.c\"
${CMAKE_C_COMPILER} -c \"${CMAKE_BINARY_DIR}/dummy.c\" -o \"${CMAKE_BINARY_DIR}/dummy.o\"
${CMAKE_AR} crs \"${OUTPUT_DIRECTORY}/libc4.a\" \"${CMAKE_BINARY_DIR}/dummy.o\"
rm -f \"${CMAKE_BINARY_DIR}/dummy.o\"

# Process each library
")

        # Create script commands for each library using generator expressions
        set(SCRIPT_LIBRARY_COMMANDS "")
        foreach(lib ${ALL_LIBS_TO_COMBINE})
            string(APPEND SCRIPT_LIBRARY_COMMANDS "
echo \"Processing ${lib}\"
mkdir -p \"${CMAKE_BINARY_DIR}/combine_work/${lib}\"
cd \"${CMAKE_BINARY_DIR}/combine_work/${lib}\"
${CMAKE_AR} x \"$<TARGET_FILE:${lib}>\"
if ls *.o > /dev/null 2>&1; then
  ${CMAKE_AR} rs \"${OUTPUT_DIRECTORY}/libc4.a\" *.o
else
  echo \"No object files found in ${lib}\"
fi
")
        endforeach()

        # Create script footer
        set(SCRIPT_FOOTER "
echo \"Running ranlib\"
${CMAKE_RANLIB} \"${OUTPUT_DIRECTORY}/libc4.a\"
echo \"Cleaning up\"
rm -rf \"${CMAKE_BINARY_DIR}/combine_work\"
echo \"Combined library created at: ${OUTPUT_DIRECTORY}/libc4.a\"
")

        # Use file(GENERATE) to expand generator expressions at generate time
        file(GENERATE 
            OUTPUT "${CMAKE_BINARY_DIR}/combine_libs.sh"
            CONTENT "${SCRIPT_HEADER}${SCRIPT_LIBRARY_COMMANDS}${SCRIPT_FOOTER}"
            FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        )
        
        # Add custom command to run the script after c4_combined target is built
        add_custom_command(
            TARGET c4_combined POST_BUILD
            COMMAND "${CMAKE_BINARY_DIR}/combine_libs.sh"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Combining static libraries"
            VERBATIM
        )
        
        # Install the combined library
        install(TARGETS c4_combined
            ARCHIVE DESTINATION lib
        )
    endif()
endif()

