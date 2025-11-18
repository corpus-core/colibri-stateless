# Copyright (c) 2025 corpus.core
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of
# this software and associated documentation files (the "Software"), to deal in
# the Software without restriction, including without limitation the rights to
# use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
# the Software, and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
# FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# SPDX-License-Identifier: MIT

# Finde alle C-Dateien im aktuellen Verzeichnis
file(GLOB TEST_SOURCES test_*.c)

set(TEST_INCLUDES
  ${unity_SOURCE_DIR}/src 
  ${CMAKE_SOURCE_DIR}/src 
)

# Add server-specific includes if HTTP_SERVER is enabled
if(HTTP_SERVER)
  list(APPEND TEST_INCLUDES
    ${CMAKE_SOURCE_DIR}/src/prover
    ${CMAKE_SOURCE_DIR}/src/verifier
    ${CMAKE_BINARY_DIR}/_deps/llhttp-src/include
    ${CMAKE_BINARY_DIR}/_deps/libuv-src/include
  )
endif()

# We build libuv via libs/libuv and expose INTERFACE target 'libuv'
set(HAVE_LIBUV OFF)
if(TARGET libuv)
  set(HAVE_LIBUV ON)
endif()

#add_definitions(-DTEST_DATA_DIR=${CMAKE_SOURCE_DIR}/test/data)

foreach(test_source ${TEST_SOURCES})
    # Extrahiere den Dateinamen ohne Pfad und Suffix
    get_filename_component(test_name ${test_source} NAME_WE)

    # Erstelle eine ausführbare Datei für den Test
    add_executable(${test_name} ${test_source})

    # Füge die Include-Verzeichnisse hinzu
    target_include_directories(${test_name} PRIVATE ${TEST_INCLUDES})

    # Verknüpfe die Testausführbare mit Unity und deinem Code
    target_link_libraries(${test_name} unity verifier prover)
    
    # Add server dependencies if HTTP_SERVER is enabled
    if(HTTP_SERVER)
        target_compile_definitions(${test_name} PRIVATE HTTP_SERVER)
        # Link order for static libs:
        # 1) serverlib (needs handler symbols)
        # 2) handlers (provide eth_* symbols)
        # 3) serverio (provides uv_util symbols used by handlers)
        target_link_libraries(${test_name} serverlib ${SERVER_HANDLER_LIBS} serverio llhttp_static libuv)
        add_dependencies(${test_name} llhttp_static serverlib)
        if(TARGET serverio)
          add_dependencies(${test_name} serverio)
        endif()
        
        # Find CURL for server tests
        find_package(CURL REQUIRED)
        target_link_libraries(${test_name} CURL::libcurl)
    endif()

    # Special linking for uv_util tests even without HTTP_SERVER
    if("${test_name}" STREQUAL "test_uv_util")
        # If HTTP_SERVER is OFF (minsize/valgrind job), don't pull in libuv or uv_util.c.
        # Build a tiny binary that immediately skips at runtime.
        if(NOT HTTP_SERVER)
          target_compile_definitions(${test_name} PRIVATE NO_LIBUV=1)
        else()
          if(NOT HAVE_LIBUV)
            target_compile_definitions(${test_name} PRIVATE NO_LIBUV=1)
          endif()
          # Prefer linking serverio if available; otherwise compile uv_util.c directly
          if(TARGET serverio)
            target_link_libraries(${test_name} serverio libuv)
            target_include_directories(${test_name} PRIVATE ${CMAKE_BINARY_DIR}/_deps/libuv-src/include)
            add_dependencies(${test_name} serverio)
          else()
            message(WARNING "serverio target not found; compiling uv_util.c directly into ${test_name}")
            target_sources(${test_name} PRIVATE ${CMAKE_SOURCE_DIR}/src/server/uv_util.c)
            target_include_directories(${test_name} PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/server ${CMAKE_BINARY_DIR}/_deps/libuv-src/include)
            target_link_libraries(${test_name} util libuv)
          endif()
        endif()
    endif()

    # Suppress MSVC warning C4113 for test targets
    if(MSVC)
        target_compile_options(${test_name} PRIVATE /wd4113)
    endif()

    # Füge den Test zu CTest hinzu
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

if(MSVC)
    # Add Windows-specific definitions for test compatibility
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
endif()



# Erstelle eine ausführbare Datei für den Test
add_executable(verify_only verify_only.c)

# Füge die Include-Verzeichnisse hinzu
target_include_directories(verify_only PRIVATE  ${TEST_INCLUDES})

# Define TESTDATA_DIR
target_compile_definitions(verify_only PRIVATE TESTDATA_DIR="${CMAKE_SOURCE_DIR}/test/data")


# Verknüpfe die Testausführbare mit Unity und deinem Code
target_link_libraries(verify_only verifier)
