add_executable(c4 main.c)
target_link_libraries(c4 PRIVATE util)
set_target_properties(c4 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)