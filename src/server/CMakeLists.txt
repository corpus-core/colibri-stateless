# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

find_package(CURL REQUIRED)

add_executable(server 
  main.c
  http_client.c
  http_client_errors.c
  http_servers_select.c
  http_server.c
  handle_proof.c
  handle_verify.c
  handle_verify_storage.c
  handle_health.c
  handle_metrics.c
  cache.c
  configure.c
  store.c
  internal_calls.c
)

target_include_directories(server PUBLIC ${CMAKE_BINARY_DIR})
target_include_directories(server PUBLIC ../../libs)
target_include_directories(server PUBLIC ${CMAKE_BINARY_DIR}/_deps/llhttp-src/include)
target_include_directories(server PUBLIC ${CMAKE_BINARY_DIR}/_deps/libuv-src/include)

target_link_libraries(server PRIVATE libuv llhttp proofer util verifier CURL::libcurl ${SERVER_HANDLER_LIBS})

set_target_properties(server PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

  # Additional system libraries that might be needed
  if(UNIX AND NOT APPLE)
    target_link_libraries(server PRIVATE pthread m dl)
  elseif(APPLE)
    target_link_libraries(server PRIVATE "-framework CoreFoundation" "-framework Security")
  endif()
