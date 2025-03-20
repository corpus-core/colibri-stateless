add_executable(server server.c)
set_target_properties(server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
target_link_libraries(server PRIVATE 
    civetweb
    proofer
    verifier
)
