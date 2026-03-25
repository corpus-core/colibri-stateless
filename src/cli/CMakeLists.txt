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

option(CLI "generates the CLI tools." ON)
if (WASM OR SWIFT)
    set(CLI false)
endif()

if (CLI)    
    add_executable(colibri-prover prover.c)
    set_target_properties(colibri-prover PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    add_executable(colibri-ssz ssz.c)
    target_link_libraries(colibri-ssz PRIVATE 
      verifier
      prover
      eth_verifier
    )
    set_target_properties(colibri-ssz PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    add_custom_command(TARGET colibri-ssz POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            colibri-ssz$<TARGET_FILE_SUFFIX:colibri-ssz>
            "${CMAKE_BINARY_DIR}/bin/ssz$<TARGET_FILE_SUFFIX:colibri-ssz>"
        COMMENT "Creating symlink: ssz -> colibri-ssz"
    )

    add_executable(colibri-verifier verifier.c ../../bindings/colibri_common.c)
    set_target_properties(colibri-verifier PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    add_custom_command(TARGET colibri-verifier POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            colibri-verifier$<TARGET_FILE_SUFFIX:colibri-verifier>
            "${CMAKE_BINARY_DIR}/bin/colibri$<TARGET_FILE_SUFFIX:colibri-verifier>"
        COMMENT "Creating symlink: colibri -> colibri-verifier"
    )

    if(CURL)
        target_link_libraries(colibri-prover PRIVATE 
            curl_fetch
            prover
            verifier
        )
        target_link_libraries(colibri-verifier PRIVATE 
            curl_fetch
            prover
            verifier
            eth_verifier
            )

    else()
        target_link_libraries(colibri-prover PRIVATE 
            prover
            verifier
        )   
        target_link_libraries(colibri-verifier PRIVATE 
            prover
            verifier
            eth_verifier
            )

    endif()

    # Install targets
    include(GNUInstallDirs)
    install(TARGETS colibri-prover colibri-verifier colibri-ssz
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    # Recreate convenience symlinks: colibri -> colibri-verifier, ssz -> colibri-ssz
    install(CODE "
        execute_process(COMMAND \${CMAKE_COMMAND} -E create_symlink
            colibri-verifier \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/colibri)
        message(STATUS \"Creating symlink: colibri -> colibri-verifier\")
        execute_process(COMMAND \${CMAKE_COMMAND} -E create_symlink
            colibri-ssz \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/ssz)
        message(STATUS \"Creating symlink: ssz -> colibri-ssz\")
    ")


endif()
