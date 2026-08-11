# Copyright (c) 2026 corpus.core
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

# Node.js native addon (N-API): a self-contained `colibri_native.node` that
# statically links the full C library (including blst with platform-optimized
# assembly). Loaded by the npm package's Node entry point as a faster
# alternative to the WASM build.

include(FetchContent)

# Only the C headers are needed; N-API symbols are resolved from the host
# process (Node.js) at load time.
FetchContent_Declare(
    node_api_headers
    URL https://github.com/nodejs/node-api-headers/archive/refs/tags/v1.5.0.tar.gz
)
FetchContent_MakeAvailable(node_api_headers)

add_library(colibri_native MODULE
    src/addon.c
    ${CMAKE_SOURCE_DIR}/bindings/colibri_common.c
)

set_target_properties(colibri_native PROPERTIES
    PREFIX ""
    SUFFIX ".node"
    C_STANDARD 11
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/node-addon
)

target_include_directories(colibri_native PRIVATE
    ${node_api_headers_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/bindings
)

target_link_libraries(colibri_native PRIVATE prover verifier eth_verifier)

if(APPLE)
    # N-API symbols live in the hosting Node.js binary and are resolved at load time.
    target_link_options(colibri_native PRIVATE -undefined dynamic_lookup)
elseif(MSVC)
    # Windows requires an import library. node-api-headers ships a .def file
    # describing the N-API symbols exported by node.exe.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(NODE_API_MACHINE ARM64)
    else()
        set(NODE_API_MACHINE x64)
    endif()
    set(NODE_API_IMPLIB ${CMAKE_CURRENT_BINARY_DIR}/node_api.lib)
    add_custom_command(
        OUTPUT ${NODE_API_IMPLIB}
        COMMAND lib /nologo /def:${node_api_headers_SOURCE_DIR}/def/node_api.def /out:${NODE_API_IMPLIB} /machine:${NODE_API_MACHINE}
        DEPENDS ${node_api_headers_SOURCE_DIR}/def/node_api.def
        COMMENT "Generating node_api.lib import library"
    )
    add_custom_target(node_api_implib DEPENDS ${NODE_API_IMPLIB})
    add_dependencies(colibri_native node_api_implib)
    target_link_libraries(colibri_native PRIVATE ${NODE_API_IMPLIB})
endif()
# Linux: undefined symbols in a MODULE library are resolved by the host process at dlopen time (default behavior).
