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

add_library(eth_precompiles STATIC 
  precompiles_basic.c
)
set (DEPS util crypto)
target_include_directories(eth_precompiles INTERFACE .)
# BLST headers are needed by precompiles_bls.c (included from precompiles_basic.c)
# util links blst privately, so we must add the include path here as well.
target_include_directories(eth_precompiles PRIVATE ${CMAKE_BINARY_DIR}/_deps/blst-src/bindings)
target_link_libraries(eth_precompiles PRIVATE blst)

# Optionally embed the KZG trusted setup (only the required G2^tau compressed point)
option(ETH_PRECOMPILE_EMBED "Embed KZG trusted setup G2^tau as a generated header" ON)
set(ETH_TRUSTED_SETUP_FILE "${CMAKE_CURRENT_SOURCE_DIR}/trusted_setup.txt" CACHE FILEPATH "Path to trusted_setup.txt (c-kzg-4844 format)" FORCE)

if(ETH_PRECOMPILE_EMBED)
  # Generate a small header with the 96-byte compressed G2^tau (index 1 of G2 list)
  set(TRUSTED_SETUP_EMBED_H "${CMAKE_CURRENT_BINARY_DIR}/trusted_setup_embed.h")
  set(GEN_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/gen_trusted_setup_embed.cmake")

  add_custom_command(
    OUTPUT "${TRUSTED_SETUP_EMBED_H}"
    COMMAND ${CMAKE_COMMAND}
      -DINPUT=${ETH_TRUSTED_SETUP_FILE}
      -DOUTPUT=${TRUSTED_SETUP_EMBED_H}
      -P "${GEN_SCRIPT}"
    DEPENDS "${ETH_TRUSTED_SETUP_FILE}" "${GEN_SCRIPT}"
    COMMENT "Generating embedded trusted-setup header (G2^tau)"
    VERBATIM
  )

  add_custom_target(trusted_setup_embed ALL
    DEPENDS "${TRUSTED_SETUP_EMBED_H}"
  )

  add_dependencies(eth_precompiles trusted_setup_embed)
  target_compile_definitions(eth_precompiles PRIVATE ETH_PRECOMPILE_EMBED)
  # Ensure the generated header directory is on the include path
  target_include_directories(eth_precompiles PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()

if (INTX)
  set (DEPS ${DEPS} intx_wrapper)
endif()

# Link against eth_bn254 for BN254 precompiles
target_link_libraries(eth_precompiles PRIVATE eth_bn254)

target_link_libraries(eth_precompiles PUBLIC ${DEPS})
