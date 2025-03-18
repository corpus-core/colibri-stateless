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

# Using modern NDK with better C++20 support instead of patching

# Patch intx.hpp for Android compatibility
if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
    include(${CMAKE_CURRENT_SOURCE_DIR}/intx_patch.cmake)
endif()

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

# Use C++20 to match intx requirements
target_compile_features(intx_wrapper PRIVATE cxx_std_20)

# Add compile definitions for Android to help with C++20 compatibility
if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
    target_compile_definitions(intx_wrapper PRIVATE 
        # Ensure our fallback implementations are used on Android
        INTX_ANDROID_COMPAT
    )
endif()

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