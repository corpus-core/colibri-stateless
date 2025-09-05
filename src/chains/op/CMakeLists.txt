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




set(VERIFIER_SOURCES
  verifier/op_verify_block.c
  verifier/op_verify.c
  verifier/op_zstd.c
  ssz/op_types.c
)
set(VERIFIER_DEPS util eth_verifier zstd)



add_verifier(
  NAME op_verifier
  SOURCES ${VERIFIER_SOURCES}
  DEPENDS ${VERIFIER_DEPS}
  GET_REQ_TYPE c4_op_get_request_type
  VERIFY c4_op_verify
  METHOD_TYPE c4_op_get_method_type
)


add_proofer(
  NAME op_proofer
  SOURCES 
    proofer/op_tools.c
    proofer/op_proofer.c
    proofer/op_proof_block.c
  DEPENDS util eth_proofer eth_verifier op_verifier
  PROOF op_proofer_execute
)

add_server_handler(
  NAME op_server_handler
  SOURCES 
    server/handler.c
    server/op_preconf_capture.c
  DEPENDS op_proofer 
  INIT_FUNC op_server_init
  SHUTDOWN_FUNC op_server_shutdown
)

if(HTTP_SERVER)
    add_subdirectory(opg_bridge)
    add_dependencies(op_server_handler opg_bridge)
endif()
