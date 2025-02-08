option(CLI "generates the CLI tools." ON)
if (WASM)
    set(CLI false)
endif()

if (CLI)    

   


    add_executable(proof proofer.c)#
    set_target_properties(proof PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    add_executable(ssz ssz.c)
    target_link_libraries(ssz PRIVATE 
      verifier
      proofer
    )
    set_target_properties(ssz PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    add_executable(verify verifier.c)
    set_target_properties(verify PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    if(CURL)
        target_link_libraries(proof PRIVATE 
            curl_fetch
            proofer
            verifier
        )
        target_link_libraries(verify PRIVATE 
            curl_fetch
            verifier
        )

    else()
        target_link_libraries(proof PRIVATE 
            proofer
            verifier
        )   
        target_link_libraries(verify PRIVATE 
            verifier
        )

    endif()





endif()
