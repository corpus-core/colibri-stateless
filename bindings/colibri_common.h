/*
 * Copyright (c) 2025,2026 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef colibri_common_h__
#define colibri_common_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "prover.h"
#include "verify.h"
#include <stdbool.h>

/**
 * RPC context phases for the unified execution flow.
 */
typedef enum {
  RPC_PHASE_INIT,      ///< Initial phase: method type not yet determined
  RPC_PHASE_PROVING,   ///< Prover is active, waiting for proof
  RPC_PHASE_VERIFYING, ///< Verifier is active, verifying proof
  RPC_PHASE_RPC,       ///< Forwarding an unproofable request as data_request_t
  RPC_PHASE_DONE       ///< Terminal: result or error available
} c4_rpc_phase_t;

/**
 * Unified RPC context that manages the full lifecycle of an RPC request:
 * method type determination, proof generation (local or remote), and verification.
 */
typedef struct {
  char*           method;
  char*           params;
  chain_id_t      chain_id;
  prover_flags_t  prover_flags;
  verify_flags_t  verify_flags;
  c4_rpc_phase_t  phase;
  method_type_t   method_type;
  bool            use_remote_prover;

  prover_ctx_t*   prover;
  verify_ctx_t    verifier;
  bytes_t         proof;
  bool            proof_owned;

  data_request_t* rpc_request;
  char*           error;
} c4_rpc_ctx_t;

/**
 * Creates a new unified RPC context.
 *
 * @param method RPC method name (e.g. "eth_getBalance"). Copied internally.
 * @param params JSON array string of params. Copied internally.
 * @param chain_id target chain ID
 * @param prover_flags flags for proof generation (see `prover_flag_types_t`)
 * @param verify_flags flags for verification (see `verify_flag_t`, e.g. `VERIFY_FLAG_PAP`)
 * @param use_remote_prover if true, emit a prover data_request instead of local proving
 * @return heap-allocated context, or NULL on allocation failure
 */
c4_rpc_ctx_t* c4_rpc_ctx_create(const char* method, const char* params, chain_id_t chain_id,
                                prover_flags_t prover_flags, verify_flags_t verify_flags,
                                bool use_remote_prover);

/**
 * Executes one step of the unified RPC state machine.
 *
 * @param ctx the RPC context
 * @return `C4_SUCCESS`, `C4_ERROR`, or `C4_PENDING`
 */
c4_status_t c4_rpc_execute(c4_rpc_ctx_t* ctx);

/**
 * Returns a pointer to the `c4_state_t` that currently holds pending requests.
 *
 * Depending on the phase, this is the prover state, the verifier state,
 * or a synthetic state wrapping `rpc_request`.
 *
 * @param ctx the RPC context
 * @return pointer to the active state, or NULL if phase is INIT, RPC, or DONE
 */
c4_state_t* c4_rpc_get_state(c4_rpc_ctx_t* ctx);

/**
 * Frees the RPC context and all owned resources.
 *
 * @param ctx the RPC context (may be NULL)
 */
void c4_rpc_ctx_free(c4_rpc_ctx_t* ctx);

/* ── JSON serialization helpers shared by colibri.c and ems.c ── */

/**
 * Appends a single `data_request_t` as JSON object to the buffer.
 *
 * @param result output buffer
 * @param req the data request
 * @param req_ptr_as_string if true, emit `req_ptr` as a JSON string (for 64-bit safety); otherwise as number
 */
void c4i_add_data_request(buffer_t* result, data_request_t* req, bool req_ptr_as_string);

/**
 * Builds a full JSON status string from the current RPC context state.
 *
 * The caller must `free()` the returned string.
 *
 * @param ctx the RPC context
 * @param req_ptr_as_string if true, emit `req_ptr` values as JSON strings
 * @return heap-allocated JSON string
 */
char* c4_rpc_build_json_status(c4_rpc_ctx_t* ctx, bool req_ptr_as_string);

/**
 * Builds a JSON status string for a prover execution step.
 *
 * @param status the status code from `c4_prover_execute()`
 * @param state the prover state holding pending requests or error
 * @param proof_ptr pointer to proof data (used only on `C4_SUCCESS`)
 * @param proof_len length of proof data
 * @param req_ptr_as_string if true, emit `req_ptr` values as JSON strings
 * @return heap-allocated JSON string (caller must `free()`)
 */
char* c4i_build_prover_json_status(c4_status_t status, c4_state_t* state,
                                   void* proof_ptr, uint32_t proof_len,
                                   bool req_ptr_as_string);

/**
 * Builds a JSON status string for a verifier execution step.
 *
 * @param status the status code from `c4_verify()`
 * @param state the verifier state holding pending requests or error
 * @param result the SSZ result object (used only on `C4_SUCCESS`)
 * @param req_ptr_as_string if true, emit `req_ptr` values as JSON strings
 * @return heap-allocated JSON string (caller must `free()`)
 */
char* c4i_build_verifier_json_status(c4_status_t status, c4_state_t* state,
                                     ssz_ob_t result,
                                     bool req_ptr_as_string);

#ifdef __cplusplus
}
#endif

#endif
