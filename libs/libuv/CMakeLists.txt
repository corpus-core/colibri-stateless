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

# Fetch libuv from GitHub, the archive of the v1.50.0 commit checked by its hash
# (EXCLUDE_FROM_ALL suppresses libuv's own install rules). A local copy can be
# used instead with -DFETCHCONTENT_SOURCE_DIR_LIBUV=<dir>.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
  FetchContent_Declare(libuv
      URL "https://github.com/libuv/libuv/archive/8fb9cb919489a48880680a56efecff6a7dfb4504.tar.gz"
      URL_HASH SHA256=9e7949925f1bc0ce0b65e32c8171923edd73c8ffca81a2fbb758841826ff22e2
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      EXCLUDE_FROM_ALL)
else()
  FetchContent_Declare(libuv
      URL "https://github.com/libuv/libuv/archive/8fb9cb919489a48880680a56efecff6a7dfb4504.tar.gz"
      URL_HASH SHA256=9e7949925f1bc0ce0b65e32c8171923edd73c8ffca81a2fbb758841826ff22e2
      EXCLUDE_FROM_ALL)
endif()

# Configure build options before making available
set(LIBUV_BUILD_TESTS OFF CACHE BOOL "Build libuv tests" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

FetchContent_MakeAvailable(libuv)

# Create an INTERFACE library with include directories
add_library(libuv INTERFACE)
target_link_libraries(libuv INTERFACE uv_a)
target_include_directories(libuv INTERFACE ${libuv_SOURCE_DIR}/include)

# For backward compatibility
set(LIBUV_INCLUDE_DIR ${libuv_SOURCE_DIR}/include PARENT_SCOPE)
set(LIBUV_LIBRARY uv_a PARENT_SCOPE) 