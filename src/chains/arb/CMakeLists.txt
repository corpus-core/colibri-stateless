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

set(VERIFIER_SOURCES
  verifier/arb_verify.c
  verifier/arb_verify_block.c
  verifier/arb_batch_poster.c
)

add_verifier(
  NAME arb_verifier
  SOURCES ${VERIFIER_SOURCES}
  DEPENDS util eth_verifier zstd
  GET_REQ_TYPE c4_arb_get_request_type
  VERIFY c4_arb_verify
  METHOD_TYPE c4_arb_get_method_type
  INIT_RPC_CTX arb_init_rpc_ctx
)

add_prover(
  NAME arb_prover
  SOURCES
    prover/arb_prover.c
    prover/arb_proof_block.c
    prover/arb_batch_poster.c
  DEPENDS util eth_prover arb_verifier
  PROOF arb_prover_execute
)

add_server_handler(
  NAME arb_server_handler
  SOURCES
    server/handler.c
    server/arb_conf.c
    server/feed_client.c
    server/arb_preconf.c
  DEPENDS arb_prover
  INIT_FUNC arb_server_init
  SHUTDOWN_FUNC arb_server_shutdown
  CONFIG_FUNC arb_configure
)
