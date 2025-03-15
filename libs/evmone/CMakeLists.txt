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

# Link with the built libraries
target_link_libraries(evmone_wrapper PRIVATE
    ${BINARY_DIR}/lib/libevmone.a
    ${ethhash_BINARY_DIR}/lib/keccak/libkeccak.a
)

# Add dependency to ensure evmone is built before the wrapper
add_dependencies(evmone_wrapper evmone_external)

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