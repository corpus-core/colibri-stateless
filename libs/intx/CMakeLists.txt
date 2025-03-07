cmake_minimum_required(VERSION 3.11)

include(FetchContent)

# Fetch intx from GitHub
FetchContent_Declare(
    intx
    GIT_REPOSITORY https://github.com/chfast/intx.git
    GIT_TAG v0.10.0  # Use a specific version
)

# Make the content available
FetchContent_MakeAvailable(intx)

# Add the wrapper library
add_library(intx_wrapper
    ${CMAKE_CURRENT_SOURCE_DIR}/wrapper.cpp
)

# Link against intx
target_link_libraries(intx_wrapper PRIVATE intx::intx)

# Include directories
target_include_directories(intx_wrapper
    PUBLIC 
        # Make the C API header visible to users of this library
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE 
        # For the wrapper implementation
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Use C++17 to match intx requirements
target_compile_features(intx_wrapper PRIVATE cxx_std_17)

# Export C interface
set_target_properties(intx_wrapper PROPERTIES
    PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/intx_c_api.h"
)

# Installation rules (optional)
install(TARGETS intx_wrapper
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    PUBLIC_HEADER DESTINATION include
)

# Build the test program (optional - can be controlled with an option)
option(INTX_BUILD_TEST "Build intx test program" ON)
if(INTX_BUILD_TEST)
    add_executable(test_intx ${CMAKE_CURRENT_SOURCE_DIR}/test_intx.c)
    target_link_libraries(test_intx PRIVATE intx_wrapper)
    set_target_properties(test_intx PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
    )
endif() 