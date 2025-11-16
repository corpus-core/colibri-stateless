/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "beacon.h"
#include "logger.h"
#include "plugin.h"
#include "prover.h"
#include "server.h"
#include "verify.h"

/*
 * ============================================================================
 * VERIFY ENDPOINT - CALLBACK FLOW DOCUMENTATION
 * ============================================================================
 *
 * This file implements the verify endpoint (/rpc), which accepts JSON-RPC
 * requests, obtains/generates a proof, and then verifies it.
 *
 * The flow is two-phased with a complex callback structure due to libuv:
 *
 * PHASE 1: OBTAIN PROOF
 * =====================
 * Entry Point: c4_handle_verify_request()
 *
 * There are 3 different paths to obtain a proof:
 *
 * 1. LOCAL METHOD (no proof needed)
 *    ├─ Allocate verify_request_t
 *    ├─ Allocate empty data_request_t
 *    └─ Call prover_callback() immediately → PHASE 2
 *
 * 2. REMOTE PROVER (proof from external server)
 *    ├─ Allocate verify_request_t
 *    ├─ Create data_request_t with remote URL
 *    ├─ c4_add_request() starts async HTTP request
 *    └─ prover_callback() is called when response arrives → PHASE 2
 *
 * 3. LOCAL PROVER (generate proof locally)
 *    ├─ Allocate verify_request_t
 *    ├─ Create prover_ctx_t
 *    ├─ Create SEPARATE request_t with:
 *    │  ├─ ctx = prover_ctx_t
 *    │  ├─ cb = c4_prover_handle_request
 *    │  ├─ parent_ctx = verify_request_t  ← IMPORTANT: for callback routing
 *    │  └─ parent_cb = prover_callback   ← IMPORTANT: for callback routing
 *    ├─ Call c4_prover_handle_request()
 *    │  ├─ Generates proof
 *    │  ├─ respond() checks parent_cb != NULL
 *    │  │  └─ Calls prover_callback(client, parent_ctx, data_request_t*)
 *    │  └─ prover_request_free() cleans up request_t and prover_ctx_t
 *    └─ prover_callback() is called when proof is ready → PHASE 2
 *
 * PHASE 2: VERIFY PROOF
 * =====================
 * Entry Point: prover_callback()
 *
 * ├─ Receives proof (or error) as data_request_t
 * ├─ Initializes verify_ctx with c4_verify_init()
 * ├─ Transfers proof ownership to verify_req->proof
 * ├─ Calls verifier_handle_request() via verify_req->req.cb()
 * └─ verifier_handle_request() executes verification:
 *    ├─ c4_verify() processes the proof
 *    ├─ On C4_PENDING: c4_start_curl_requests() for additional data
 *    └─ On C4_SUCCESS/C4_ERROR: send response + free_verify_request()
 *
 * DATA STRUCTURES & THEIR RELATIONSHIPS
 * =====================================
 *
 * verify_request_t (defined below)
 * ├─ method, params, id      : Original JSON-RPC request data
 * ├─ proof                   : The proof blob (ownership here!)
 * ├─ ctx (verify_ctx_t)      : Verification context with state
 * └─ req (request_t)         : Request handler for phase 2
 *    ├─ client               : HTTP client connection
 *    ├─ cb                   : verifier_handle_request (phase 2 handler)
 *    └─ ctx                  : Pointer back to verify_request_t (circular ref)
 *
 * For LOCAL PROVER, an additional SEPARATE request_t is created:
 * request_t (for prover)
 * ├─ ctx                     : prover_ctx_t*
 * ├─ cb                      : c4_prover_handle_request
 * ├─ parent_ctx              : verify_request_t* (for callback routing)
 * └─ parent_cb               : prover_callback (bridge to phase 2)
 *
 * MEMORY MANAGEMENT & CLEANUP PATHS
 * ==================================
 *
 * Every path MUST end with free_verify_request()!
 *
 * Cleanup occurs in:
 * 1. c4_handle_verify_request() on validation errors
 * 2. prover_callback() on proof errors or verify init errors
 * 3. verifier_handle_request() after successful verification or error
 *
 * free_verify_request() frees:
 * ├─ verify_req->ctx via c4_verify_free_data()
 * ├─ verify_req->method (string)
 * ├─ verify_req->proof.data (proof blob)
 * └─ verify_req itself
 *
 * Note: verify_req->req (embedded struct) is NOT freed separately
 * Note: The separate request_t for LOCAL PROVER is freed by prover_request_free()
 *
 * WHY IS IT SO COMPLEX?
 * =====================
 *
 * The design uses "parent_ctx" and "parent_cb" to create a generic mechanism:
 * The prover (handle_proof.c) can either respond directly to clients OR
 * serve as a sub-request for the verifier.
 *
 * The respond() function in handle_proof.c checks:
 *   if (parent_cb && parent_ctx)
 *     parent_cb(client, parent_ctx, data_request_t*)  // Callback mode
 *   else
 *     c4_http_respond(...)                            // Direct mode
 *
 * This enables code reuse but makes the flow harder to follow.
 *
 * ============================================================================
 */

/**
 * Context structure for a verify request.
 * Holds all data needed during the entire verify process
 * (proof acquisition + verification).
 */
typedef struct {
  char*        method; // JSON-RPC method (allocated, must be freed)
  json_t       params; // JSON-RPC parameters (references original string)
  json_t       id;     // JSON-RPC ID (references original string)
  bytes_t      proof;  // The proof blob (allocated, must be freed)
  verify_ctx_t ctx;    // Verification context (contains state with requests)
  request_t    req;    // Request handler for phase 2 (embedded, not allocated)
} verify_request_t;

/**
 * Retrieves the client state for a chain from storage.
 * The client state represents synced periods and trusted block hashes.
 *
 * @param chain_id The chain ID
 * @return Client state as bytes_t or NULL_BYTES on error
 *
 * Thread-Safety: Initializes storage on first call (not thread-safe!, but no problem within the event loop)
 */
static bytes_t get_client_state(chain_id_t chain_id) {
  static bool server_storage_initialized = false;

  // Initialize server storage on first call
  if (!server_storage_initialized) {
    c4_init_server_storage();
    server_storage_initialized = true;
  }

  uint8_t          buf[100];
  buffer_t         buffer = stack_buffer(buf);
  buffer_t         result = {0};
  storage_plugin_t storage;
  c4_get_storage_config(&storage);

  return storage.get(bprintf(&buffer, "states_%l", (uint64_t) chain_id), &result) ? result.data : NULL_BYTES;
}

/**
 * Frees all resources of a verify_request_t.
 *
 * IMPORTANT: This function MUST be called at the end of every possible path!
 *
 * Frees:
 * - verify_ctx state (contains requests and errors)
 * - method string
 * - proof blob
 * - verify_request_t itself
 *
 * @param verify_req The verify request to free
 */
static void free_verify_request(verify_request_t* verify_req) {
  c4_verify_free_data(&verify_req->ctx);
  safe_free(verify_req->method);
  safe_free(verify_req->proof.data);
  safe_free(verify_req);
}

/**
 * PHASE 2 HANDLER: Executes proof verification.
 *
 * Callback function for verify_req->req.cb
 * Called after the proof is available and ready for verification.
 *
 * Flow:
 * 1. Check for failed sub-requests (c4_check_retry_request)
 * 2. Execute verification with c4_verify()
 * 3. On C4_PENDING: Start additional HTTP requests if needed
 * 4. On C4_SUCCESS/C4_ERROR: Send JSON-RPC response + cleanup
 *
 * @param req Request object (req->ctx points to verify_request_t)
 */
static void verifier_handle_request(request_t* req) {
  verify_request_t* verify_req = (verify_request_t*) req->ctx;

  // Check if any sub-requests failed and need retry
  if (c4_check_retry_request(req)) return;

  switch (c4_verify(&verify_req->ctx)) {
    case C4_SUCCESS: {
      buffer_t buf = {0};
      bprintf(&buf, "{\"id\": %J, \"result\": %Z}", verify_req->id, verify_req->ctx.data);
      c4_http_respond(req->client, 200, "application/json", buf.data);
      buffer_free(&buf);
      free_verify_request(verify_req);
      return;
    }
    case C4_ERROR: {
      buffer_t buf = {0};
      bprintf(&buf, "{\"id\": %J, \"error\":\"%S\"}", verify_req->id, verify_req->ctx.state.error);
      c4_http_respond(req->client, 200, "application/json", buf.data);
      buffer_free(&buf);
      free_verify_request(verify_req);
      return;
    }
    case C4_PENDING:
      if (c4_state_get_pending_request(&verify_req->ctx.state)) // there are pending requests, let's take care of them first
        c4_start_curl_requests(&verify_req->req, &verify_req->ctx.state);
      else {
        // No prover available and no pending requests - this shouldn't happen
        buffer_t buf = {0};
        bprintf(&buf, "{\"id\": %J, \"error\":\"%s\"}", verify_req->id, "No prover available");
        c4_http_respond(req->client, 200, "application/json", buf.data);
        buffer_free(&buf);
        free_verify_request(verify_req);
      }
  }
}

/**
 * BRIDGE BETWEEN PHASE 1 AND PHASE 2
 *
 * Callback function called once the proof is available.
 * Acts as a bridge between proof acquisition (phase 1) and verification (phase 2).
 *
 * Called from:
 * 1. LOCAL METHOD: Directly from c4_handle_verify_request with empty data_request_t
 * 2. REMOTE PROVER: From c4_add_request_response after HTTP request completes
 * 3. LOCAL PROVER: From respond() in handle_proof.c via parent_cb mechanism
 *
 * Flow:
 * 1. Check for errors in req (e.g. HTTP error, no response)
 * 2. Initialize verification context with c4_verify_init()
 * 3. Transfer proof ownership to verify_req->proof
 * 4. Start phase 2 via verify_req->req.cb() → verifier_handle_request()
 *
 * @param client HTTP client connection
 * @param data Pointer to verify_request_t (as void* for generic callback signature)
 * @param req The data_request_t with the proof or error message
 */
static void prover_callback(client_t* client, void* data, data_request_t* req) {
  verify_request_t* verify_req         = (verify_request_t*) data;
  c4_state_t        data_request_state = {.requests = req};

  // Error in proof retrieval/generation
  if (req->error) {
    c4_write_error_response(client, 500, req->error);
    c4_state_free(&data_request_state);
    free_verify_request(verify_req);
    return;
  }

  // For proofable methods, we must have a proof
  if (!req->response.data && c4_get_method_type(http_server.chain_id, verify_req->method) == METHOD_PROOFABLE) {
    buffer_t buffer = {0};
    bprintf(&buffer, "{\"id\": %J, \"error\":\"Internal prover error: no proof available\"}", verify_req->id);
    c4_http_respond(client, 200, "application/json", buffer.data);
    buffer_free(&buffer);
    c4_state_free(&data_request_state);
    free_verify_request(verify_req);
    return;
  }

  // Initialize verification context with the proof
  if (c4_verify_init(&verify_req->ctx, req->response, verify_req->method, verify_req->params, http_server.chain_id) != C4_SUCCESS) {
    buffer_t buffer = {0};
    bprintf(&buffer, "{\"id\": %J, \"error\":\"%S\"}", verify_req->id, verify_req->ctx.state.error);
    c4_http_respond(client, 200, "application/json", buffer.data);
    buffer_free(&buffer);
    c4_state_free(&data_request_state);
    free_verify_request(verify_req);
    return;
  }

  // Transfer proof ownership to verify_req (will be freed in free_verify_request)
  verify_req->proof = req->response;
  req->response     = NULL_BYTES; // Prevent double-free

  // Start Phase 2: Verification -> calling verifier_handle_request
  verify_req->req.cb(&verify_req->req);

  // Clean up the data_request (but not the proof, which was transferred)
  c4_state_free(&data_request_state);
}

/**
 * ENTRY POINT: HTTP handler for /rpc endpoint
 *
 * Processes JSON-RPC requests, obtains a proof (phase 1) and verifies it (phase 2).
 *
 * Supported method types:
 * - METHOD_LOCAL: No proof needed (e.g. eth_blockNumber)
 * - METHOD_PROOFABLE: Requires proof (e.g. eth_getBalance)
 * - METHOD_UNPROOFABLE: Not supported
 * - METHOD_NOT_SUPPORTED: Not supported
 * - METHOD_UNDEFINED: Unknown
 *
 * @param client HTTP client with request.method, request.path, request.payload
 * @return true if request was handled, false otherwise
 */
bool c4_handle_verify_request(client_t* client) {
  // Filter: Only handle POST /rpc requests
  if (client->request.method != C4_DATA_METHOD_POST || strncmp(client->request.path, "/rpc", 4) != 0)
    return false;

  // Parse JSON-RPC request
  json_t rpc_req = json_parse((char*) client->request.payload);
  if (rpc_req.type != JSON_TYPE_OBJECT) {
    c4_write_error_response(client, 400, "Invalid request, expected a JSON-RPC request");
    return true;
  }

  // Allocate and initialize verify_request_t
  // This structure will hold all context throughout the entire process
  verify_request_t* verify_req = (verify_request_t*) safe_calloc(1, sizeof(verify_request_t));
  verify_req->req.client       = client;
  verify_req->req.cb           = verifier_handle_request; // Phase 2 handler
  verify_req->req.ctx          = verify_req;              // Circular reference for context retrieval

  // Extract JSON-RPC fields
  json_t method      = json_get(rpc_req, "method");
  verify_req->params = json_get(rpc_req, "params");
  verify_req->id     = json_get(rpc_req, "id");

  // Validate JSON-RPC structure
  if (method.type != JSON_TYPE_STRING || verify_req->params.type != JSON_TYPE_ARRAY) {
    safe_free(verify_req);
    c4_write_error_response(client, 400, "Invalid request");
    return true;
  }
  else
    verify_req->method = bprintf(NULL, "%j", method);

  // Determine method type and start Phase 1 (proof retrieval/generation)
  switch (c4_get_method_type(http_server.chain_id, verify_req->method)) {

    case METHOD_UNDEFINED: {
      free_verify_request(verify_req);
      c4_write_error_response(client, 400, "Method not known");
      return true;
    }

    case METHOD_NOT_SUPPORTED: {
      free_verify_request(verify_req);
      c4_write_error_response(client, 400, "Method not supported");
      return true;
    }

    case METHOD_UNPROOFABLE: {
      free_verify_request(verify_req);
      c4_write_error_response(client, 400, "Method unproofable");
      return true;
    }

    case METHOD_LOCAL: {
      // No proof needed - create empty data_request_t and go directly to Phase 2
      prover_callback(client, verify_req, (data_request_t*) safe_calloc(1, sizeof(data_request_t)));
      return true;
    }

    case METHOD_PROOFABLE: {

      // Get client_state for proof generation/verification
      bytes_t client_state = get_client_state(http_server.chain_id);

      if (c4_get_server_list(C4_DATA_TYPE_PROVER)->count) {
        // REMOTE PROVER PATH
        // We have a remote prover configured, use it to get the proof
        buffer_t buffer = {0};
        bprintf(&buffer, "{\"method\":\"%s\",\"params\":%j,\"c4\":\"0x%x\"}", verify_req->method, verify_req->params, client_state);
        safe_free(client_state.data);

        data_request_t* req = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
        req->method         = C4_DATA_METHOD_POST;
        req->chain_id       = http_server.chain_id;
        req->type           = C4_DATA_TYPE_PROVER;
        req->encoding       = C4_DATA_ENCODING_SSZ;
        req->payload        = buffer.data;

        // Start async HTTP request, prover_callback will be called with the result
        c4_add_request(client, req, verify_req, prover_callback);
      }
      else {
        // LOCAL PROVER PATH
        // Use local prover to generate the proof
        // This uses the parent_ctx/parent_cb mechanism to route back to prover_callback

        char*         params_str = bprintf(NULL, "%J", verify_req->params);
        request_t*    req        = (request_t*) safe_calloc(1, sizeof(request_t));
        prover_ctx_t* ctx        = c4_prover_create(
            verify_req->method,
            params_str,
            (chain_id_t) http_server.chain_id,
            C4_PROVER_FLAG_UV_SERVER_CTX | C4_PROVER_FLAG_INCLUDE_CODE | (http_server.period_store ? C4_PROVER_FLAG_CHAIN_STORE : 0));

        // Setup request_t for prover (SEPARATE from verify_req->req!)
        req->start_time = current_ms();
        req->client     = client;
        req->cb         = c4_prover_handle_request; // Prover handler
        req->ctx        = ctx;                      // Context for prover

        // IMPORTANT: parent_ctx and parent_cb enable routing back to prover_callback
        // The respond() function in handle_proof.c checks these and when set calls:
        //   parent_cb(client, parent_ctx, data_request_t*)
        // instead of responding directly
        req->parent_ctx = verify_req;      // Callback routing context
        req->parent_cb  = prover_callback; // Callback routing function

        ctx->client_state = client_state;

        safe_free(params_str);

        // Start proof generation
        // May call prover_callback immediately or later (async)
        // The separate request_t will be freed by prover_request_free()
        req->cb(req);
      }
    }
  }

  return true;
}
