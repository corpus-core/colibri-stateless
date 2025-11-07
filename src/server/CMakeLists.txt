# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

find_package(CURL REQUIRED)

# Generate embedded HTML for config UI
set(CONFIG_HTML_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/web_ui/config.html)
set(CONFIG_HTML_HEADER ${CMAKE_CURRENT_BINARY_DIR}/config_html.h)

add_custom_command(
  OUTPUT ${CONFIG_HTML_HEADER}
  COMMAND ${CMAKE_COMMAND} 
    -DINPUT_FILE=${CONFIG_HTML_INPUT}
    -DOUTPUT_FILE=${CONFIG_HTML_HEADER}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/web_ui/embed_html.cmake
  DEPENDS ${CONFIG_HTML_INPUT} ${CMAKE_CURRENT_SOURCE_DIR}/web_ui/embed_html.cmake
  COMMENT "Embedding config.html into C header"
  VERBATIM
)

add_custom_target(generate_config_html DEPENDS ${CONFIG_HTML_HEADER})

# Generate embedded YAML for OpenAPI specification
set(OPENAPI_YAML_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/openapi.yaml)
set(OPENAPI_YAML_HEADER ${CMAKE_CURRENT_BINARY_DIR}/openapi_yaml.h)

add_custom_command(
  OUTPUT ${OPENAPI_YAML_HEADER}
  COMMAND ${CMAKE_COMMAND} 
    -DINPUT_FILE=${OPENAPI_YAML_INPUT}
    -DOUTPUT_FILE=${OPENAPI_YAML_HEADER}
    -DVARIABLE_NAME=openapi_yaml
    -P ${CMAKE_CURRENT_SOURCE_DIR}/web_ui/embed_html.cmake
  DEPENDS ${OPENAPI_YAML_INPUT} ${CMAKE_CURRENT_SOURCE_DIR}/web_ui/embed_html.cmake
  COMMENT "Embedding openapi.yaml into C header"
  VERBATIM
)

add_custom_target(generate_openapi_yaml DEPENDS ${OPENAPI_YAML_HEADER})

# Create a static library with all server logic (for testing)
add_library(serverlib STATIC
  server_control.c
  tracing.c
  http_client.c
  http_client_errors.c
  http_servers_select.c
  http_head_poller.c
  http_server.c
  handle_proof.c
  handle_verify.c
  handle_unverified_rpc.c
  handle_verify_storage.c
  handle_health.c
  handle_metrics.c
  handle_config.c
  handle_openapi.c
  cache.c
  configure.c
  store.c
  internal_calls.c
)

target_include_directories(serverlib PUBLIC ${CMAKE_BINARY_DIR})
target_include_directories(serverlib PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(serverlib PUBLIC ../../libs)
target_include_directories(serverlib PUBLIC ${CMAKE_BINARY_DIR}/_deps/llhttp-src/include)
target_include_directories(serverlib PUBLIC ${CMAKE_BINARY_DIR}/_deps/libuv-src/include)

# Make sure config HTML and OpenAPI YAML are generated before building serverlib
add_dependencies(serverlib generate_config_html generate_openapi_yaml)

target_link_libraries(serverlib PUBLIC libuv llhttp prover util verifier CURL::libcurl ${SERVER_HANDLER_LIBS})

# Additional system libraries that might be needed
if(UNIX AND NOT APPLE)
  target_link_libraries(serverlib PUBLIC pthread m dl)
elseif(APPLE)
  target_link_libraries(serverlib PUBLIC "-framework CoreFoundation" "-framework Security")
endif()

# Create the executable with just main()
add_executable(colibri-server main.c)

target_link_libraries(colibri-server PRIVATE serverlib)

set_target_properties(colibri-server PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
