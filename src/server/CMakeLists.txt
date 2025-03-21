find_package(CURL REQUIRED)

add_executable(server 
  main.c
  http_client.c
  http_server.c
  proof_handler.c
)

target_link_libraries(server PRIVATE libuv llhttp proofer util verifier CURL::libcurl)

set_target_properties(server PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

  # Additional system libraries that might be needed
  if(UNIX AND NOT APPLE)
    target_link_libraries(server PRIVATE pthread m dl)
  elseif(APPLE)
    target_link_libraries(server PRIVATE "-framework CoreFoundation" "-framework Security")
  endif()
