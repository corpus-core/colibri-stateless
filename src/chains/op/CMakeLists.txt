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



if (EVMLIGHT OR EVMONE)
    if (EVMONE) 
       add_definitions(-DEVMONE)
    else()
       add_definitions(-DEVMLIGHT)
    endif()
    add_definitions(-DEVM -DINTX)
    if (PRECOMPILES_RIPEMD160)
        add_definitions(-DPRECOMPILED_RIPEMD160)
    endif()
endif()


set(VERIFIER_SOURCES
  verifier/op_verify.c
  verifier/op_zstd.c
  verifier/op_chains_conf.c
  ssz/op_types.c
)


if(ETH_ACCOUNT)
  set(VERIFIER_SOURCES ${VERIFIER_SOURCES} verifier/op_verify_account.c)
  add_definitions(-DETH_ACCOUNT)
endif()

if(ETH_TX)
  set(VERIFIER_SOURCES ${VERIFIER_SOURCES} verifier/op_verify_tx.c)
  add_definitions(-DETH_TX)
endif()

if(ETH_BLOCK)
  set(VERIFIER_SOURCES ${VERIFIER_SOURCES} verifier/op_verify_block.c)
  add_definitions(-DETH_BLOCK)
endif()

if(ETH_RECEIPT)
  set(VERIFIER_SOURCES ${VERIFIER_SOURCES} verifier/op_verify_receipt.c)
  add_definitions(-DETH_RECEIPT)
endif()

if(ETH_LOGS)
  set(VERIFIER_SOURCES ${VERIFIER_SOURCES} verifier/op_verify_logs.c)
  add_definitions(-DETH_LOGS)
endif()

if(ETH_CALL)
  set(VERIFIER_SOURCES ${VERIFIER_SOURCES} verifier/op_verify_call.c verifier/op_verify_simulate.c)
  add_definitions(-DETH_CALL)
endif()

add_verifier(
  NAME op_verifier
  SOURCES ${VERIFIER_SOURCES}
  DEPENDS util eth_verifier zstd
  GET_REQ_TYPE c4_op_get_request_type
  VERIFY c4_op_verify
  METHOD_TYPE c4_op_get_method_type
)


add_prover(
  NAME op_prover
  SOURCES 
    prover/op_tools.c
    prover/op_prover.c
    prover/op_proof_block.c
    prover/op_proof_transaction.c
    prover/op_proof_receipt.c
    prover/op_proof_logs.c
    prover/op_proof_call.c
    prover/op_proof_account.c
  DEPENDS util eth_prover op_verifier
  PROOF op_prover_execute
)

# Build bridges first, so we can determine dependencies
if(HTTP_SERVER)
    # Kona-Bridge (neue, native OP-Stack-kompatible Bridge)
    add_subdirectory(kona_bridge)
    
    # Go-Bridge removed - using Kona-Bridge only
endif()

# Determine dependencies based on available bridges
set(OP_SERVER_DEPENDS op_prover)
if(HTTP_SERVER AND TARGET kona_preconf_capture)
    list(APPEND OP_SERVER_DEPENDS kona_preconf_capture)
    message(STATUS "✅ Kona-P2P bridge will be linked to op_server_handler")
elseif(HTTP_SERVER)
    message(STATUS "⚠️  Kona-P2P bridge not available, using Go bridge only")
endif()

# Create server handler with correct dependencies
add_server_handler(
  NAME op_server_handler
  SOURCES 
    server/handler.c
    server/preconf.c
  DEPENDS ${OP_SERVER_DEPENDS}
  INIT_FUNC op_server_init
  SHUTDOWN_FUNC op_server_shutdown
  METRICS_FUNC op_server_metrics
)

# Go bridge dependencies removed - using Kona-Bridge only
