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

# Use the tarball URL as recommended instead of git repo
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
  # CMake 3.24+: Use DOWNLOAD_EXTRACT_TIMESTAMP
  FetchContent_Declare(llhttp
    URL "https://github.com/nodejs/llhttp/archive/refs/tags/release/v9.2.1.tar.gz"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
else()
  # CMake < 3.24: Don't use DOWNLOAD_EXTRACT_TIMESTAMP
  FetchContent_Declare(llhttp
    URL "https://github.com/nodejs/llhttp/archive/refs/tags/release/v9.2.1.tar.gz")
endif()

# Set cache variables for static library build
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
set(BUILD_STATIC_LIBS ON CACHE INTERNAL "")

# Temporarily suppress CMake deprecation warnings for external llhttp project
set(_original_warn_deprecated ${CMAKE_WARN_DEPRECATED})
set(CMAKE_WARN_DEPRECATED OFF)

FetchContent_MakeAvailable(llhttp)

# Restore original warning setting
set(CMAKE_WARN_DEPRECATED ${_original_warn_deprecated})

# Create an INTERFACE library with include directories
add_library(llhttp INTERFACE)
target_link_libraries(llhttp INTERFACE llhttp_static)
target_include_directories(llhttp INTERFACE ${llhttp_SOURCE_DIR}/include)

# For backward compatibility
set(LLHTTP_INCLUDE_DIR ${llhttp_SOURCE_DIR}/include PARENT_SCOPE)
set(LLHTTP_LIBRARY llhttp_static PARENT_SCOPE) 