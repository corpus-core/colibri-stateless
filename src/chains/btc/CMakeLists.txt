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

if(NOT MSVC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-excess-initializers")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-excess-initializers")
endif()

add_verifier(
  NAME btc_verifier
  SOURCES
    crypto/btc_hash.c
    crypto/btc_merkle.c
    crypto/btc_target.c
    crypto/btc_tx.c
    ssz/btc_types.c
    verifier/btc_client.c
    verifier/btc_header.c
    verifier/btc_proof.c
    verifier/btc_verify.c
    verifier/btc_chain_props.c
  DEPENDS util
  GET_REQ_TYPE c4_btc_get_request_type
  VERIFY c4_btc_verify
  METHOD_TYPE c4_btc_get_method_type
)

add_prover(
  NAME btc_prover
  SOURCES
    prover/btc_prover.c
  DEPENDS util btc_verifier verifier
  PROOF btc_prover_execute
)

add_chain_props(FUNC c4_btc_chain_props)

if(TARGET btc_verifier)
  target_include_directories(btc_verifier PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/crypto)
  target_include_directories(btc_verifier PRIVATE ${CMAKE_SOURCE_DIR}/libs/crypto)
endif()
