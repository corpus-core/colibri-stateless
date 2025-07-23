# Copyright (c) 2025 corpus.core
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of
# this software and associated documentation files (the "Software"), to deal in
# the Software without restriction, including without limitation the rights to
# use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
# the Software, and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
# FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# SPDX-License-Identifier: MIT

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
add_library(intx_wrapper STATIC EXCLUDE_FROM_ALL
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

# Suppress specific warnings for intx dependency if using Clang or GCC
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(intx_wrapper PRIVATE -Wno-deprecated-literal-operator)
endif()

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
