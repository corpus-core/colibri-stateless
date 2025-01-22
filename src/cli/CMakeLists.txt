option(CLI "generates the CLI tools." ON)

if (CLI)    
    add_executable(ssz ssz.c)
    target_link_libraries(ssz PRIVATE 
      util
      verifier
      crypto
      blst
    )
    set_target_properties(ssz PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    add_executable(verify verifier.c)
    target_link_libraries(verify PRIVATE 
      util
      verifier
      crypto
      blst
    )
    set_target_properties(verify PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )





endif()
