# Copyright (c) 2025,2026 corpus.core GmbH
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

# EEZ (Ethereum Economic Zone) rollups reuse the Ethereum proof types. The module only
# adds the `eezL1Proof` block proof: the L2 block hash is read from the storage of the
# EEZ proxy contract on L1.

if(NOT MSVC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-excess-initializers")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-excess-initializers")
endif()

# The Ethereum headers included by this module are configured by these definitions.
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

if(ETH_ACCOUNT)
  add_definitions(-DETH_ACCOUNT)
endif()
if(ETH_TX)
  add_definitions(-DETH_TX)
endif()
if(ETH_BLOCK)
  add_definitions(-DETH_BLOCK)
endif()
if(ETH_RECEIPT)
  add_definitions(-DETH_RECEIPT)
endif()
if(ETH_LOGS)
  add_definitions(-DETH_LOGS)
endif()
if(ETH_CALL)
  add_definitions(-DETH_CALL)
endif()

add_verifier(
  NAME eez_verifier
  SOURCES
    verifier/eez_verify.c
    verifier/eez_verify_block.c
    verifier/eez_chains_conf.c
  DEPENDS util eth_verifier
  GET_REQ_TYPE c4_eez_get_request_type
  VERIFY c4_eez_verify
  METHOD_TYPE c4_eez_get_method_type
  INIT_RPC_CTX eez_init_rpc_ctx
)

add_prover(
  NAME eez_prover
  SOURCES
    prover/eez_prover.c
    prover/eez_proof_block.c
  DEPENDS util eth_prover eez_verifier
  PROOF eez_prover_execute
)

# A prover server for an EEZ chain serves the block proof of hybrid mode. The handler
# itself is part of the Ethereum server module.
add_server_handler(
  NAME eez_server_handler
  SOURCES
    server/eez_server.c
  DEPENDS eez_prover eth_server_handler
  INIT_FUNC eez_server_init
)
