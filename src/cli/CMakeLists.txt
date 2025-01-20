option(CLI "generates the CLI tools." ON)

if (CLI)    
    add_executable(ssz ssz.c)
    target_link_libraries(ssz PRIVATE 
      util
      verifier
    )
    set_target_properties(ssz PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()
