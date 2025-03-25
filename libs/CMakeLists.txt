option(CURL "if true curl will be used as transport" ON)


add_subdirectory(crypto)
add_subdirectory(blst)
if(CURL)
    add_subdirectory(curl)
endif()

if (INTX)
    add_subdirectory(intx)
endif()

if(EVMONE)
    add_subdirectory(evmone)
endif()

if (HTTP_SERVER)
    add_subdirectory(libuv)
    add_subdirectory(llhttp)
endif()