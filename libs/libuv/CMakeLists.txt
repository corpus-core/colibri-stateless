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

# Set policy CMP0135 to NEW if CMake version >= 3.24
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
  cmake_policy(SET CMP0135 NEW)
endif()

# Fetch libuv from GitHub using URL approach for consistency
FetchContent_Declare(libuv
    URL "https://github.com/libuv/libuv/archive/refs/tags/v1.50.0.tar.gz"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# Configure build options before making available
set(LIBUV_BUILD_TESTS OFF CACHE BOOL "Build libuv tests" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

# Make the content available
FetchContent_MakeAvailable(libuv)

# Create an INTERFACE library with include directories
add_library(libuv INTERFACE)
target_link_libraries(libuv INTERFACE uv_a)
target_include_directories(libuv INTERFACE ${libuv_SOURCE_DIR}/include)

# For backward compatibility
set(LIBUV_INCLUDE_DIR ${libuv_SOURCE_DIR}/include PARENT_SCOPE)
set(LIBUV_LIBRARY uv_a PARENT_SCOPE) 