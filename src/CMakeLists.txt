
add_subdirectory(util)
if(VERIFIER)
    add_subdirectory(verifier)
endif()
if(PROOFER)
    add_subdirectory(proofer)
endif()
add_subdirectory(chains)
if(CLI)
    add_subdirectory(cli)
endif()
if (HTTP_SERVER)
    add_subdirectory(server)
endif()
