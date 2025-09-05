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

# Fetch ZSTD from GitHub using URL approach for consistency
FetchContent_Declare(zstd
    URL "https://github.com/facebook/zstd/archive/refs/tags/v1.5.6.tar.gz"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# Configure build options before making available
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "Build zstd programs" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "Build zstd tests" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "Build shared libraries" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "Build static libraries" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

# Make the content available
FetchContent_MakeAvailable(zstd)

# ZSTD builds its library under build/cmake subdirectory
set(ZSTD_BUILD_DIR ${zstd_SOURCE_DIR}/build/cmake)

# Add the ZSTD cmake subdirectory with EXCLUDE_FROM_ALL
# This ensures ZSTD is only built when needed as a dependency
add_subdirectory(${ZSTD_BUILD_DIR} zstd_build EXCLUDE_FROM_ALL)

# Create an INTERFACE library with include directories
add_library(zstd INTERFACE)
target_link_libraries(zstd INTERFACE libzstd_static)
target_include_directories(zstd INTERFACE ${zstd_SOURCE_DIR}/lib)

# For backward compatibility
set(ZSTD_INCLUDE_DIR ${zstd_SOURCE_DIR}/lib PARENT_SCOPE)
set(ZSTD_LIBRARY libzstd_static PARENT_SCOPE)
