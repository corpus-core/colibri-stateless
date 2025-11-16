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

set(EMC_PROPS "-s ALLOW_MEMORY_GROWTH=1 -s EXPORT_ES6=1 -s MODULARIZE=1 -s NODEJS_CATCH_REJECTION=0 -s EXPORT_NAME=c4w  -s FILESYSTEM=0 -s ENVIRONMENT=web,node  -sEXPORTED_FUNCTIONS=[\"_malloc\",\"_free\"]  -s 'EXPORTED_RUNTIME_METHODS=[\"ccall\", \"cwrap\",\"UTF8ToString\",\"stringToUTF8\"]'")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  -Wpointer-arith ")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS}  -Wpointer-arith ")


IF (CMAKE_BUILD_TYPE MATCHES Debug)
   set(EMC_PROPS "${EMC_PROPS} -s WASM=1 -g -s SAFE_HEAP=1 -s ASSERTIONS=1 -s STACK_OVERFLOW_CHECK=1 -s RUNTIME_LOGGING=1 ")
ELSE (CMAKE_BUILD_TYPE MATCHES Debug)
   set(EMC_PROPS "${EMC_PROPS} -s WASM=1 -O3 ")
ENDIF (CMAKE_BUILD_TYPE MATCHES Debug)

option(WASM_EMBED "Embed WASM into JS (SINGLE_FILE=1)" ON)
if (WASM_EMBED)
   set(EMC_PROPS "${EMC_PROPS} -s SINGLE_FILE=1")
endif()

# Define a custom output directory for Emscripten and TypeScript artifacts
set(EMSCRIPTEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/emscripten")

# Ensure the directory exists
file(MAKE_DIRECTORY ${EMSCRIPTEN_OUTPUT_DIR})

# Get absolute path for Webpack config
get_filename_component(EMSCRIPTEN_OUTPUT_DIR_ABS ${EMSCRIPTEN_OUTPUT_DIR} ABSOLUTE)

# Configure the Webpack helper file
set(WEBPACK_CONFIG_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/webpack/webpack_build_config.js.in")
set(WEBPACK_CONFIG_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/webpack/webpack_build_config.mjs")
configure_file(
  ${WEBPACK_CONFIG_TEMPLATE}
  ${WEBPACK_CONFIG_OUTPUT}
  @ONLY
)

add_executable(c4w ems.c)
target_link_libraries(c4w prover verifier)
set_target_properties(c4w PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${EMSCRIPTEN_OUTPUT_DIR}
    LINK_FLAGS "${EMC_PROPS}"
)

# Copy package.json to the output directory
add_custom_command(
    TARGET c4w POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_SOURCE_DIR}/bindings/emscripten/c4_logo.png
    ${EMSCRIPTEN_OUTPUT_DIR}/c4_logo.png
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_SOURCE_DIR}/bindings/emscripten/README.md
    ${EMSCRIPTEN_OUTPUT_DIR}/README.md
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_SOURCE_DIR}/bindings/emscripten/package.json
    ${EMSCRIPTEN_OUTPUT_DIR}/package.json
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_SOURCE_DIR}/LICENSE
    ${EMSCRIPTEN_OUTPUT_DIR}/LICENSE
    COMMENT "Copying package.json, LICENSE, and c4_logo.png to output directory"
)

# Add a custom target to compile TypeScript
add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/bindings/emscripten/node_modules/timestamp
    COMMAND ${CMAKE_COMMAND} -E echo "Installing Node.js modules..."
    COMMAND npm install
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_SOURCE_DIR}/bindings/emscripten/node_modules/timestamp
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bindings/emscripten
    DEPENDS ${CMAKE_SOURCE_DIR}/bindings/emscripten/package.json
    COMMENT "Installing Node.js dependencies if necessary"
)
add_custom_target(install_node_modules ALL
    DEPENDS ${CMAKE_SOURCE_DIR}/bindings/emscripten/node_modules/timestamp
)

# Find all TypeScript source files dynamically
file(GLOB_RECURSE TS_SOURCES "${CMAKE_SOURCE_DIR}/bindings/emscripten/src/*.ts")

# Add a custom target to compile TypeScript
add_custom_command(
    OUTPUT ${EMSCRIPTEN_OUTPUT_DIR}/index.js
    COMMAND ${CMAKE_COMMAND} -E echo "Compiling TypeScript..."
    COMMAND npx tsc --outDir ${EMSCRIPTEN_OUTPUT_DIR} --declaration
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bindings/emscripten
    DEPENDS 
        ${CMAKE_SOURCE_DIR}/bindings/emscripten/node_modules/timestamp
        ${TS_SOURCES}
        ${CMAKE_SOURCE_DIR}/bindings/emscripten/tsconfig.json
    COMMENT "Building TypeScript sources"
)

# Add a custom target that depends on the TypeScript output file.
# Building this target will trigger the add_custom_command above.
add_custom_target(compile_typescript
    DEPENDS ${EMSCRIPTEN_OUTPUT_DIR}/index.js
)

# Ensure the TypeScript compilation happens after node modules are installed
add_dependencies(compile_typescript install_node_modules)

# Ensure the main target c4w depends on the TypeScript compilation finishing
add_dependencies(c4w compile_typescript)

# Define the full path to the generated JS module relative to the test directory
# We need to calculate the relative path from the test directory to the module
file(RELATIVE_PATH REL_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/test" # From this directory
    "${EMSCRIPTEN_OUTPUT_DIR}/index.js" # To this file
)
set(EMSCRIPTEN_MODULE_PATH "${REL_MODULE_PATH}") # Use relative path for easier import

# Configure the test helper file using the relative path
set(TEST_CONFIG_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/test/test_config.js.in")
set(TEST_CONFIG_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/test/test_config.js") # Output next to tests
configure_file(
  ${TEST_CONFIG_TEMPLATE}
  ${TEST_CONFIG_OUTPUT}
  @ONLY
)

# Add a custom target to run npm tests
add_custom_target(test
    COMMAND npm test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bindings/emscripten
    DEPENDS ${EMSCRIPTEN_OUTPUT_DIR}/index.js
    COMMENT "Running npm tests"
)

