option(CURL "if true curl will be used as transport" ON)


add_subdirectory(crypto)
add_subdirectory(blst)

if(CURL)
    add_subdirectory(curl)
endif()

add_subdirectory(intx) # intx is needed for evmone and for the precompile, but will onlöy be build if needed
add_subdirectory(evmone) # evmone will only be build if needed

if (HTTP_SERVER)
    add_subdirectory(libuv)
    add_subdirectory(llhttp)
endif()