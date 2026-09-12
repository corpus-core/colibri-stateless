# Copyright (c) 2025,2026 corpus.core
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
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
# SPDX-License-Identifier: MIT

# Host / unified-RPC layer (`c4_rpc_ctx_t`). Used by the CLI, tests, WASM
# (`ems.c`), the Node addon (`addon.c`), and the generic C FFI (`api_ffi`).
add_library(api STATIC
  colibri_common.c
)
target_include_directories(api PUBLIC .)
target_link_libraries(api PUBLIC prover verifier)
if(TARGET eth_verifier)
  target_link_libraries(api PUBLIC eth_verifier)
endif()
set_property(TARGET api PROPERTY POSITION_INDEPENDENT_CODE ON)

# Generic C FFI (`c4_*` in colibri.h). Language bindings that wrap a C ABI
# (Python, Kotlin/SWIG, Swift, Dart, Rust, SHAREDLIB) link this target.
# Emscripten and the Node addon keep their own wrappers and link `api` only.
add_library(api_ffi STATIC
  colibri.c
)
target_include_directories(api_ffi PUBLIC .)
target_link_libraries(api_ffi PUBLIC api)
set_property(TARGET api_ffi PROPERTY POSITION_INDEPENDENT_CODE ON)
