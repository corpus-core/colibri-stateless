cmake_minimum_required(VERSION 3.10...3.32)
project(c4)
option(SHAREDLIB "if true the shared library will be build" OFF)
option(TEST "if true the test will be build" OFF)
option(COVERAGE "if true the coverage will be build" OFF)
option(WASM "if true the wasm will be build" OFF)
option(KOTLIN "if true the kotlin bindings will be build" OFF)
option(SWIFT "if true the swift bindings will be build" OFF)
option(BLS_DESERIALIZE "if true bls keys will be stored deserialized, which increases the state per sync committee period from 25kB to 50kB, but make verification 5x faster" ON)

if(KOTLIN)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(CURL OFF)
endif()

# Definiere eine Funktion, um iOS-spezifische Eigenschaften für ein Target zu setzen
function(set_ios_target_properties target)
    if(SWIFT)
        set_target_properties(${target} PROPERTIES
            XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator"
            XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH NO
            XCODE_ATTRIBUTE_VALID_ARCHS "arm64 x86_64"
        )
    endif()
endfunction()

if(SWIFT)
    # Wenn für Swift gebaut wird, verwende iOS Toolchain nur für die Swift-Bibliothek
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/.github/ios.toolchain.cmake CACHE FILEPATH "iOS Toolchain file")
    set(PLATFORM "OS64COMBINED" CACHE STRING "iOS Platform target")
    set(CURL OFF)
    
    # Speichere die iOS-spezifischen Flags
    set(IOS_CMAKE_C_FLAGS ${CMAKE_C_FLAGS})
    set(IOS_CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
    
    # Setze temporär die Standard-Flags für die Host-Builds
    set(CMAKE_C_FLAGS ${SAVED_CMAKE_C_FLAGS})
    set(CMAKE_CXX_FLAGS ${SAVED_CMAKE_CXX_FLAGS})
endif()

if(CURL)
    add_definitions(-DUSE_CURL)
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

set(OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
string(TOUPPER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_UPPER)


if(CMAKE_BUILD_TYPE_UPPER STREQUAL "MINSIZEREL")

    if(POLICY CMP0069)
        cmake_policy(SET CMP0069 NEW)
    endif()
    # Aktivieren von Link-Time Optimization
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    # Compiler-Flags für Größe-Optimierung
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")

    # Unterscheide zwischen Plattformen
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        if(APPLE)
            # macOS-spezifische Flags
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -dead_strip")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -dead_strip")
        else()
            # Standard (z. B. für Linux)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --gc-sections")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} --gc-sections")
        endif()
    endif()

endif()


add_subdirectory(src)
add_subdirectory(libs)

if(TEST)
    enable_testing()
    add_subdirectory(test)

    # Set CTest arguments to include --output-on-failure
    set(CMAKE_CTEST_ARGUMENTS "--output-on-failure")
endif()

# Define a custom target named 'doc'
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
#    COMMAND ${CMAKE_COMMAND} -E env bash -c "docker run --rm --platform=linux/amd64 -v ${CMAKE_BINARY_DIR}/out:/out c4_valgrind"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating the valgrind image"
)

if(SHAREDLIB)
    # Erstelle die Shared Library und verlinke die statischen Bibliotheken
    add_library(c4Lib SHARED src/util/version.c)
    target_link_libraries(c4Lib PRIVATE crypto verifier util blst)
    set_target_properties(c4Lib PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIRECTORY}
        VERSION 1.0
        SOVERSION 1
    )
endif()

if(WASM)
    add_subdirectory(bindings/emscripten)
endif()

if(KOTLIN)
    add_subdirectory(bindings/kotlin)
endif()

if(SWIFT)
    # Stelle die iOS-Flags für die Swift-Bibliothek wieder her
    set(CMAKE_C_FLAGS ${IOS_CMAKE_C_FLAGS})
    set(CMAKE_CXX_FLAGS ${IOS_CMAKE_CXX_FLAGS})
    add_subdirectory(bindings/swift)
endif()