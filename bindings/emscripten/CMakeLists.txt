set(EMC_PROPS "-s ALLOW_MEMORY_GROWTH=1 -s EXPORT_ES6=1 -s MODULARIZE=1 -s NODEJS_CATCH_REJECTION=0 -s EXPORT_NAME=c4w  -s FILESYSTEM=0 -s ENVIRONMENT=web,node  -sEXPORTED_FUNCTIONS=[\"_malloc\",\"_free\"]  -s 'EXPORTED_RUNTIME_METHODS=[\"ccall\", \"cwrap\",\"UTF8ToString\",\"stringToUTF8\"]'")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  -Wpointer-arith ")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS}  -Wpointer-arith ")


IF (CMAKE_BUILD_TYPE MATCHES Debug)
   set(EMC_PROPS "${EMC_PROPS} -s WASM=1 -g -s SAFE_HEAP=1 -s ASSERTIONS=1 -s STACK_OVERFLOW_CHECK=1 -s RUNTIME_LOGGING=1 ")
ELSE (CMAKE_BUILD_TYPE MATCHES Debug)
   set(EMC_PROPS "${EMC_PROPS} -s WASM=1 -O3 ")
ENDIF (CMAKE_BUILD_TYPE MATCHES Debug)

#if (WASM_EMBED)
   set(EMC_PROPS "${EMC_PROPS} -s SINGLE_FILE=1")
#endif(WASM_EMBED)

# Define a custom output directory for Emscripten and TypeScript artifacts
set(EMSCRIPTEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/emscripten")

# Ensure the directory exists
file(MAKE_DIRECTORY ${EMSCRIPTEN_OUTPUT_DIR})


add_executable(c4w ems.c)
target_link_libraries(c4w proofer verifier)
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

# Add a custom target to compile TypeScript
add_custom_target(compile_typescript ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Compiling TypeScript..."
    COMMAND npx tsc --outDir ${EMSCRIPTEN_OUTPUT_DIR} --declaration
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bindings/emscripten
    COMMENT "Building TypeScript sources"
)

# Ensure the TypeScript compilation happens after node modules are installed
add_dependencies(compile_typescript install_node_modules)

# Ensure the TypeScript compilation happens before the main build
add_dependencies(c4w compile_typescript)

# Add a custom target to run npm tests
add_custom_target(test
    COMMAND npm test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bindings/emscripten
#    DEPENDS compile_typescript
    COMMENT "Running npm tests"
)

