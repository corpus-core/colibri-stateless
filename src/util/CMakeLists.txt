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

option(FILE_STORAGE "if activated the verfifier will use a simple file-implementaion to store states in the current folder or in a folder specified by the env varC4_STATE_DIR" ON)
if(FILE_STORAGE AND NOT WASM)
    add_definitions(-DFILE_STORAGE)
endif()


option(PRECOMPILE_ZERO_HASHES "Enable precomputed zero hashes cache (1 KB RAM). Required for ETH_BLOCK verification but not needed for light client sync. Disable for embedded devices to save memory." ON)

if(PRECOMPILE_ZERO_HASHES)
    add_definitions(-DPRECOMPILE_ZERO_HASHES)
    message(STATUS "PRECOMPILE_ZERO_HASHES enabled (uses ~1 KB RAM for zero hash cache)")
else()
    message(STATUS "PRECOMPILE_ZERO_HASHES disabled (saves ~1 KB RAM)")
endif()

add_library(util STATIC 
  bytes.c 
  ssz.c
  ssz_merkle.c
  ssz_builder.c
  crypto.c
  plugin.c
  json.c
  json_validate.c
  logger.c
  chains.c
  state.c
  version.c
  witness.c
)
target_include_directories(util PRIVATE ../../libs/crypto ${CMAKE_BINARY_DIR}/_deps/blst-src/bindings)
target_include_directories(util INTERFACE .)
target_link_libraries(util PRIVATE crypto blst)
