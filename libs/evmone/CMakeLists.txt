# Use ExternalProject for evmone - no Hunter initialization
include(ExternalProject)
include(FetchContent)



ExternalProject_Add(
    ethhash_external
    GIT_REPOSITORY https://github.com/chfast/ethash.git
    GIT_SHALLOW TRUE
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DBUILD_SHARED_LIBS=OFF
        -DEVMONE_TESTING=OFF
        # Pass compiler settings
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        # Pass toolchain for cross-compilation if defined
        $<$<BOOL:${CMAKE_TOOLCHAIN_FILE}>:-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}>
        # Disable Hunter warnings
        -DCMAKE_WARN_DEPRECATED=OFF
    # Skip install step
    INSTALL_COMMAND ""

)
ExternalProject_Get_Property(ethhash_external SOURCE_DIR BINARY_DIR)
set(ethhash_SOURCE_DIR ${SOURCE_DIR})
set(ethhash_BINARY_DIR ${BINARY_DIR})

# Create an imported target for keccak
if(WIN32)
    set(KECCAK_LIB_PATH "${ethhash_BINARY_DIR}/lib/keccak/Release/keccak.lib")
else()
    set(KECCAK_LIB_PATH "${ethhash_BINARY_DIR}/lib/keccak/libkeccak.a")
endif()
add_custom_target(build_keccak
  COMMAND ${CMAKE_COMMAND} --build ${ethhash_BINARY_DIR} --target keccak
  COMMENT "Building keccak library directly"
)
add_dependencies(build_keccak ethhash_external)

add_library(keccak STATIC IMPORTED)
set_target_properties(keccak PROPERTIES IMPORTED_LOCATION ${KECCAK_LIB_PATH})
add_dependencies(keccak ethhash_external)

# Define the external project for evmone
ExternalProject_Add(
    evmone_external
    GIT_REPOSITORY https://github.com/corpus-core/evmone.git
    GIT_SHALLOW TRUE
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DBUILD_SHARED_LIBS=OFF
        -DEVMONE_TESTING=OFF
        -DEVMONE_PRECOMPILES=OFF
        # Pass compiler settings
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        # Pass toolchain for cross-compilation if defined
        $<$<BOOL:${CMAKE_TOOLCHAIN_FILE}>:-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}>
        # Disable Hunter warnings
        -DCMAKE_WARN_DEPRECATED=OFF
    # Skip install step
    INSTALL_COMMAND ""
)

# Get properties about the external project
ExternalProject_Get_Property(evmone_external SOURCE_DIR BINARY_DIR)

# Create our wrapper library
add_library(evmone_wrapper STATIC 
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
        ${SOURCE_DIR}/include       # evmone private headers
    PUBLIC
        ${SOURCE_DIR}/evmc/include  # EVMC headers needed by clients
        ${BINARY_DIR}/evmc/include  # Generated EVMC headers
        ${CMAKE_CURRENT_SOURCE_DIR} # Our wrapper header
)

# Set platform-specific library name and path
if(WIN32)
    set(EVMONE_LIB_PATH "${BINARY_DIR}/lib/Release/evmone.lib")
else()
    set(EVMONE_LIB_PATH "${BINARY_DIR}/lib/libevmone.a")
endif()

# Target to copy keccak library to a predictable location
add_custom_target(
    copy_keccak
    DEPENDS keccak
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/libs/evmone/lib
    COMMAND ${CMAKE_COMMAND} -E echo "Checking for keccak at ${KECCAK_LIB_PATH}"
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/libs/evmone/lib/keccak_placeholder.a
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${KECCAK_LIB_PATH} ${CMAKE_BINARY_DIR}/libs/evmone/lib/ || echo "Warning: keccak library not found, using placeholder"
    COMMENT "Copying keccak library to standard location (with fallback)"
)

if(ANDROID)
    # For Android, add the direct build dependency
    add_dependencies(copy_keccak build_keccak)
endif()

# Link with the built libraries
if(ANDROID)
    # For Android, link against the placeholder if the actual library is not found
    target_link_libraries(evmone_wrapper PRIVATE
        ${EVMONE_LIB_PATH}
    )
    
    # Check if keccak library exists, otherwise use the placeholder
    if(EXISTS "${CMAKE_BINARY_DIR}/libs/evmone/lib/libkeccak.a")
        target_link_libraries(evmone_wrapper PRIVATE
            "${CMAKE_BINARY_DIR}/libs/evmone/lib/libkeccak.a"
        )
    else()
        target_link_libraries(evmone_wrapper PRIVATE
            "${CMAKE_BINARY_DIR}/libs/evmone/lib/keccak_placeholder.a"
        )
    endif()
else()
    target_link_libraries(evmone_wrapper PRIVATE
        ${EVMONE_LIB_PATH}
        keccak
    )
endif()

# Make wrapper library export its dependencies
set_target_properties(evmone_wrapper PROPERTIES
    INTERFACE_LINK_LIBRARIES "${EVMONE_LIB_PATH};${KECCAK_LIB_PATH}"
)

# Add dependency to ensure evmone is built before the wrapper
add_dependencies(evmone_wrapper evmone_external copy_keccak)

# Create alias for convenience
add_library(evmone ALIAS evmone_wrapper)

# Install targets
install(TARGETS evmone_wrapper
    ARCHIVE DESTINATION lib
    INCLUDES DESTINATION include
)

install(FILES evmone_c_wrapper.h
    DESTINATION include
)

# Create a global dependency hook that can be used by other parts of the project
add_custom_target(evmone_dependencies ALL
    DEPENDS ethhash_external copy_keccak
    COMMENT "Ensuring all evmone dependencies are built"
)