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
if (WASM)
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
    )
    set_target_properties(colibri-ssz PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    add_executable(colibri-verifier verifier.c)
    set_target_properties(colibri-verifier PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    if(CURL)
        target_link_libraries(colibri-prover PRIVATE 
            curl_fetch
            prover
            verifier
        )
        target_link_libraries(colibri-verifier PRIVATE 
            curl_fetch
            verifier
        )

    else()
        target_link_libraries(colibri-prover PRIVATE 
            prover
            verifier
        )   
        target_link_libraries(colibri-verifier PRIVATE 
            verifier
        )

    endif()





endif()
