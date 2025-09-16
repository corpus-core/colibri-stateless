# CMakeLists.txt für Kona-P2P Bridge Integration (Statisch)

cmake_minimum_required(VERSION 3.16)

# Option um Kona-Bridge zu aktivieren/deaktivieren
option(BUILD_KONA_BRIDGE "Build Kona-P2P bridge for native OP-Stack compatibility" ON)

if(NOT BUILD_KONA_BRIDGE)
    message(STATUS "Kona bridge disabled - skipping")
    return()
endif()

# Prüfe ob Rust verfügbar ist
find_program(CARGO_EXECUTABLE cargo)
if(NOT CARGO_EXECUTABLE)
    message(WARNING "Cargo not found - Kona bridge will not be built. Install Rust from https://rustup.rs/")
    message(STATUS "To install Rust: curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh")
    return()
endif()

message(STATUS "Found Cargo: ${CARGO_EXECUTABLE}")
message(STATUS "Building Kona-P2P bridge with static linking")

# Build die Rust-Library (statisch)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libkona_bridge_built.stamp
    COMMAND ${CMAKE_COMMAND} -E env CARGO_BUILD_TARGET_DIR=${CMAKE_CURRENT_BINARY_DIR}/target MACOSX_DEPLOYMENT_TARGET=15.0
            ${CARGO_EXECUTABLE} build --release --lib
    COMMAND ${CMAKE_COMMAND} -E copy 
            ${CMAKE_CURRENT_BINARY_DIR}/target/release/libkona_bridge.a
            ${CMAKE_BINARY_DIR}/lib/libkona_bridge.a
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/libkona_bridge_built.stamp
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/lib.rs
            ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml
    COMMENT "Building Kona-P2P Rust static library"
)

add_custom_target(kona_bridge_rust
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/libkona_bridge_built.stamp
)

# Importiere die statische Rust-Library
add_library(kona_bridge_lib STATIC IMPORTED GLOBAL)
set_target_properties(kona_bridge_lib PROPERTIES
    IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/lib/libkona_bridge.a
)
add_dependencies(kona_bridge_lib kona_bridge_rust)

# System-Dependencies für Rust (Platform-spezifisch)
if(APPLE)
    # Fix macOS deployment target warnings
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "macOS deployment target")
    set(KONA_SYSTEM_LIBS "-framework Security -framework CoreFoundation -framework SystemConfiguration")
elseif(WIN32)
    set(KONA_SYSTEM_LIBS "ws2_32" "userenv" "advapi32")
else()
    set(KONA_SYSTEM_LIBS "dl" "pthread" "m")
endif()

# Kona-Preconf-Capture (direkte Integration)
add_library(kona_preconf_capture STATIC
    ../server/kona_preconf_capture.c
)

target_include_directories(kona_preconf_capture PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/util
    ${CMAKE_BINARY_DIR}/_deps/libuv-src/include
)

target_link_libraries(kona_preconf_capture
    kona_bridge_lib
    libuv
    ${KONA_SYSTEM_LIBS}
)

# Add op_chains_conf.c as a separate object to avoid circular dependencies
add_library(op_chains_conf_obj OBJECT ../verifier/op_chains_conf.c)
target_include_directories(op_chains_conf_obj PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(kona_preconf_capture op_chains_conf_obj)

# Definiere KONA_BRIDGE_AVAILABLE für bedingte Kompilierung
target_compile_definitions(kona_preconf_capture PUBLIC KONA_BRIDGE_AVAILABLE=1)

# Standalone Kona-Bridge Binary (falls gewünscht)
if(BUILD_KONA_BRIDGE_STANDALONE)
    add_custom_target(kona_bridge_standalone
        DEPENDS kona_bridge_rust
        COMMENT "Kona bridge standalone binary built by Rust"
    )
endif()

# Installation
install(FILES kona_bridge.h
    DESTINATION include
)

install(FILES ${CMAKE_BINARY_DIR}/lib/libkona_bridge.${KONA_LIB_EXT}
    DESTINATION lib
    OPTIONAL
)

if(EXISTS ${CMAKE_BINARY_DIR}/bin/kona_bridge)
    install(PROGRAMS ${CMAKE_BINARY_DIR}/bin/kona_bridge
        DESTINATION bin
    )
endif()

# Test (optional) - nur wenn test_integration.c existiert
if(BUILD_TESTING AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_integration.c)
    add_executable(test_kona_integration
        test_integration.c
    )
    
    target_link_libraries(test_kona_integration
        kona_preconf_capture
    )
    
    add_test(NAME kona_integration_test
        COMMAND test_kona_integration
    )
elseif(BUILD_TESTING)
    message(STATUS "Skipping Kona integration test - test_integration.c not found")
endif()
