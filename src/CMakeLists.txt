option(MESSAGES "if activated the binaries will contain error messages, but for embedded systems this is not needed and can be turned off to save memory" ON)
if(MESSAGES)
    add_definitions(-DMESSAGES)
endif()

add_subdirectory(cli)
add_subdirectory(util)
add_subdirectory(verifier)
add_subdirectory(proofer)