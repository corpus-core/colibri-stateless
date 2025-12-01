/*
 * Copyright (c) 2025 corpus.core
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

#ifndef C4_STATE_H
#define C4_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"
#include "chains.h"
#include "crypto.h"
#include "json.h"
#include <string.h>

// : APIs

// :: Internal APIs

// ::: state.h
//
// ## Architecture
//
// ### Asynchronous Execution Model
//
// This module implements an asynchronous state machine for proof generation and verification.
// The state object (`c4_state_t`) is typically part of a context struct (`prover_ctx_t` or
// `verify_ctx_t`) and manages external data requests that cannot be fulfilled synchronously.
//
// #### Execution Flow
//
// The main execution function (e.g., `c4_prover_execute`) is called repeatedly in a loop until
// it returns either `C4_SUCCESS` or `C4_ERROR`. When external data is needed, the function:
//
// 1. Creates a `data_request_t` and adds it to the state
// 2. Returns `C4_PENDING` to signal that data is required
// 3. The host system fetches the required data
// 4. The host system calls the execution function again
// 5. The function resumes and processes the now-available data
//
// This design allows the host system to use native async technologies (promises, async/await,
// futures, etc.) in the language bindings while keeping the C core synchronous and portable.
//
// #### Request Lifecycle
//
// ```mermaid
// stateDiagram-v2
//     [*] --> Executing: c4_prover_execute()
//     Executing --> NeedData: Data required
//     NeedData --> CreateRequest: Create data_request_t
//     CreateRequest --> AddToState: c4_state_add_request()
//     AddToState --> ReturnPending: return C4_PENDING
//     ReturnPending --> HostFetch: Host system fetches data
//     HostFetch --> SetResponse: Set response/error in request
//     SetResponse --> Executing: c4_prover_execute() again
//     Executing --> CheckData: Check if data available
//     CheckData --> ProcessData: Data available
//     ProcessData --> Executing: Continue processing
//     Executing --> ReturnSuccess: All done
//     Executing --> ReturnError: Error occurred
//     ReturnSuccess --> [*]
//     ReturnError --> [*]
// ```
//
// ### Host System Implementation
//
// The host system is responsible for executing data requests. When `C4_PENDING` is returned,
// the host system should iterate through all pending requests using `c4_state_get_pending_request()`
// and execute them according to the following rules:
//
// #### Request Execution
//
// For each `data_request_t`, the host system must:
//
// 1. **Select Node**: The host system maintains a list of nodes (max 16) for each `data_request_type_t`
//    - Beacon API nodes for `C4_DATA_TYPE_BEACON_API`
//    - Ethereum RPC nodes for `C4_DATA_TYPE_ETH_RPC`
//    - Checkpointz servers for `C4_DATA_TYPE_CHECKPOINTZ`
//    - etc.
//
// 2. **Apply Filters**:
//    - Skip nodes where `node_exclude_mask & (1 << node_index)` is set
//    - Prefer nodes matching `preferred_client_type` (if set, 0 = any client)
//
// 3. **Build Request**:
//    - Use `method` field for HTTP method (`C4_DATA_METHOD_GET`, `POST`, etc.)
//    - Use `url` field for the endpoint path (if set)
//    - Use `payload` for POST body (if set, typically JSON-RPC for `C4_DATA_TYPE_ETH_RPC`)
//    - Set Content-Type and Accept headers based on `encoding`:
//      - `C4_DATA_ENCODING_JSON`: application/json
//      - `C4_DATA_ENCODING_SSZ`: application/octet-stream
//
// 4. **Execute Request**:
//    - Try the selected node
//    - On failure: try next available node (respecting exclude mask)
//    - On success: set `response` and `response_node_index`
//    - If all nodes fail: set `error`
//
// 5. **Caching**:
//    - If `ttl` is set (> 0), the response may be cached for the specified duration
//    - Cache key should include request type, URL, and payload
//
// 6. **Set Result**:
//    - **Success**: Allocate memory for `response.data`, copy data, set `response.len` and `response_node_index`
//    - **Failure**: Allocate memory for `error` string describing the failure
//    - **Important**: Both `response.data` and `error` will be freed by `c4_state_free()` - use `malloc`/`strdup`
//
// #### Example Implementation
//
// See `bindings/emscripten/src/index.ts` function `handle_request()` for a complete TypeScript implementation.
//
// ### Best Practices
//
// To minimize the number of execution cycles, developers should:
//
// - Collect all required data requests as early as possible in the execution
// - Use `TRY_ADD_ASYNC` to queue multiple requests before returning `C4_PENDING`
// - The host system should fetch all pending requests in parallel when possible
// - Cache responses according to `ttl` to avoid redundant network requests
// - Implement smart node selection (e.g., prefer faster nodes, track reliability)
//
// ### Example Usage
//
// ```c
// prover_ctx_t* ctx = c4_prover_create("eth_getBlockByNumber", "[\"latest\", false]", chain_id, C4_PROVER_FLAG_INCLUDE_CODE);
//
// // Execute prover in a loop:
// data_request_t* data_request = NULL;
// bytes_t proof = {0};
// while (true) {
//   switch (c4_prover_execute(ctx)) {
//     case C4_SUCCESS:
//       proof = bytes_dup(ctx->proof);
//       break;
//     case C4_PENDING:
//       // Fetch all pending requests (can be done in parallel)
//       while ((data_request = c4_state_get_pending_request(&ctx->state)))
//          fetch_data(data_request);
//       break;
//     case C4_ERROR:
//       printf("Error: %s\n", ctx->state.error);
//       break;
//   }
// }
// c4_prover_free(ctx);
// ```
//
// ### Macros for Async Control Flow
//
// The `TRY_ASYNC` family of macros simplifies error handling and control flow:
//
// - `TRY_ASYNC(fn)`: Execute function, return immediately if not C4_SUCCESS
// - `TRY_ADD_ASYNC(status, fn)`: Queue multiple requests, only fail on C4_ERROR
// - `TRY_2_ASYNC(fn1, fn2)`: Execute two functions in parallel
// - `TRY_ASYNC_FINAL(fn, cleanup)`: Always run cleanup regardless of result
// - `TRY_ASYNC_CATCH(fn, cleanup)`: Run cleanup only on failure
//
// These macros handle the repetitive pattern of checking status codes and allow
// the developer to focus on the business logic.

// Constants for state management
#define C4_BYTES32_SIZE     32  ///< Size of a 32-byte hash/identifier
#define C4_MAX_NODES        16  ///< Maximum number of nodes that can be tracked in node_exclude_mask
#define C4_MAX_MOCKNAME_LEN 100 ///< Maximum length of mock request names in test mode

/**
 * Defines the type of data source for a request.
 */
typedef enum {
  C4_DATA_TYPE_BEACON_API  = 0, ///< Request is handled by the Beacon API
  C4_DATA_TYPE_ETH_RPC     = 1, ///< Request is handled by the Ethereum RPC
  C4_DATA_TYPE_REST_API    = 2, ///< Request is handled by a generic REST API
  C4_DATA_TYPE_INTERN      = 3, ///< Request is handled internally within the prover server
  C4_DATA_TYPE_PROVER      = 4, ///< Request is handled by the prover server
  C4_DATA_TYPE_CHECKPOINTZ = 5  ///< Request is handled by a checkpointz server

} data_request_type_t;

/**
 * Defines the encoding format for request/response data.
 */
typedef enum {
  C4_DATA_ENCODING_JSON = 0, ///< Data is encoded in JSON format
  C4_DATA_ENCODING_SSZ  = 1  ///< Data is encoded in SSZ (Simple Serialize) format
} data_request_encoding_t;

/**
 * HTTP methods for data requests.
 */
typedef enum {
  C4_DATA_METHOD_GET    = 0, ///< HTTP GET method
  C4_DATA_METHOD_POST   = 1, ///< HTTP POST method
  C4_DATA_METHOD_PUT    = 2, ///< HTTP PUT method
  C4_DATA_METHOD_DELETE = 3  ///< HTTP DELETE method
} data_request_method_t;

/**
 * Status codes for asynchronous operations.
 */
typedef enum {
  C4_SUCCESS = 0,  ///< Operation completed successfully
  C4_ERROR   = -1, ///< Operation failed with an error
  C4_PENDING = 2   ///< Operation is pending and requires external data
} c4_status_t;

/**
 * Represents a single asynchronous data request in the state machine.
 *
 * This structure encapsulates all information needed to make, track, and retry
 * external data requests. Requests can be to various data sources (Beacon API,
 * Eth RPC, etc.) and support retry logic with node exclusion.
 */
typedef struct data_request {
  chain_id_t              chain_id;              ///< The blockchain chain ID for this request
  data_request_type_t     type;                  ///< Type of data source (Beacon API, Eth RPC, etc.)
  data_request_encoding_t encoding;              ///< Encoding format for request/response (JSON or SSZ)
  char*                   url;                   ///< URL endpoint for the request (may be NULL for RPC)
  data_request_method_t   method;                ///< HTTP method to use (GET, POST, etc.)
  bytes_t                 payload;               ///< Request payload data (e.g., for POST requests)
  bytes_t                 response;              ///< Response data received from the request
  uint16_t                response_node_index;   ///< Index of the node that responded with the result
  uint16_t                node_exclude_mask;     ///< Bitlist marking nodes to exclude when retrying (bit 0 = index 0, max 16 nodes)
  uint32_t                preferred_client_type; ///< Preferred beacon client type bitmask (0 = any client)
  char*                   error;                 ///< Error message if request failed (NULL if no error)
  struct data_request*    next;                  ///< Pointer to next request in linked list
  bytes32_t               id;                    ///< Unique identifier for this request (32-byte hash)
  uint32_t                ttl;                   ///< Time-to-live or retry counter for this request
  bool                    validated;             ///< Whether the response has been validated
} data_request_t;

/**
 * Global state container for asynchronous operations.
 *
 * Contains all pending data requests and accumulated error messages.
 * Used by the async state machine to track operations that require
 * external data.
 */
typedef struct {
  data_request_t* requests; ///< Linked list of data requests (NULL if no requests)
  char*           error;    ///< Accumulated error messages (NULL if no errors)
} c4_state_t;

/**
 * Frees all resources associated with a state object.
 *
 * Releases all memory allocated for data requests, URLs, payloads, responses,
 * and error messages. Safe to call with partially initialized state objects.
 *
 * @param state Pointer to the state object to free
 */
void c4_state_free(c4_state_t* state);

/**
 * Finds a data request by its unique identifier.
 *
 * Performs a linear search through the request list to find a request
 * with the matching 32-byte ID.
 *
 * @param state Pointer to the state object
 * @param id 32-byte identifier to search for
 * @return Pointer to the matching request, or NULL if not found
 */
data_request_t* c4_state_get_data_request_by_id(c4_state_t* state, bytes32_t id);

/**
 * Finds a data request by its URL.
 *
 * Performs a linear search through the request list to find a request
 * with the matching URL string.
 *
 * @param state Pointer to the state object
 * @param url URL string to search for
 * @return Pointer to the matching request, or NULL if not found
 */
data_request_t* c4_state_get_data_request_by_url(c4_state_t* state, char* url);

/**
 * Checks if a request is still pending.
 *
 * A request is considered pending if it has no error and no response data yet.
 *
 * @param req Pointer to the request to check
 * @return true if pending, false if completed or failed
 */
bool c4_state_is_pending(data_request_t* req);

/**
 * Adds a new data request to the state.
 *
 * Automatically generates a unique ID for the request if not already set
 * (hash of payload or URL). Adds the request to the front of the linked list.
 *
 * @param state Pointer to the state object
 * @param data_request Pointer to the request to add (ownership transfers to state)
 */
void c4_state_add_request(c4_state_t* state, data_request_t* data_request) M_TAKE(2);

/**
 * Gets the first pending request from the state.
 *
 * Searches through the request list and returns the first request that
 * is still pending (has no error and no response).
 *
 * @param state Pointer to the state object
 * @return Pointer to the first pending request, or NULL if none pending
 */
data_request_t* c4_state_get_pending_request(c4_state_t* state);

/**
 * Adds an error message to the state.
 *
 * Appends the error message to any existing error messages, separated by newlines.
 * Handles NULL error parameter gracefully by using a generic message.
 *
 * @param state Pointer to the state object
 * @param error Error message to add (can be NULL)
 * @return Always returns C4_ERROR
 */
c4_status_t c4_state_add_error(c4_state_t* state, const char* error);

/**
 * **TRY_ASYNC(fn)** - Executes an async function and returns early if not successful.
 *
 * This macro is used to chain asynchronous operations. If the function
 * returns anything other than C4_SUCCESS (i.e., C4_ERROR or C4_PENDING),
 * the current function returns immediately with that status.
 *
 * @param fn Function call that returns c4_status_t
 *
 * ```c
 * TRY_ASYNC(fetch_block_header(ctx, block_number));
 * TRY_ASYNC(validate_block_header(ctx));
 * ```
 */
#define TRY_ASYNC(fn)                      \
  do {                                     \
    c4_status_t state = fn;                \
    if (state != C4_SUCCESS) return state; \
  } while (0)

/**
 * **TRY_ADD_ASYNC(status, fn)** - Executes an async function but continues unless an error occurs.
 *
 * This macro is used when you want to create multiple async requests in parallel.
 * It updates the status variable to C4_PENDING if the function is pending, but
 * only returns immediately on C4_ERROR. This allows multiple requests to be
 * queued before returning.
 *
 * @param status Variable to store the cumulative status (should be c4_status_t)
 * @param fn Function call that returns c4_status_t
 *
 * ```c
 * c4_status_t status = C4_SUCCESS;
 * TRY_ADD_ASYNC(status, fetch_block_header(ctx, block1));
 * TRY_ADD_ASYNC(status, fetch_block_header(ctx, block2));
 * return status; // Returns C4_PENDING if any request is pending
 * ```
 */
#define TRY_ADD_ASYNC(status, fn)            \
  do {                                       \
    c4_status_t state = fn;                  \
    if (state == C4_ERROR) return C4_ERROR;  \
    if (state == C4_PENDING) status = state; \
  } while (0)

/**
 * **TRY_2_ASYNC(fn1, fn2)** - Executes two async functions in parallel and returns first non-success status.
 *
 * Both functions are executed before checking results, allowing them to potentially
 * run in parallel. Returns the first non-success status encountered.
 *
 * @param fn1 First function call that returns c4_status_t
 * @param fn2 Second function call that returns c4_status_t
 *
 * ```c
 * TRY_2_ASYNC(
 *   fetch_block_header(ctx, block_number),
 *   fetch_state_root(ctx, block_number)
 * );
 * ```
 */
#define TRY_2_ASYNC(fn1, fn2)                \
  do {                                       \
    c4_status_t state1 = fn1;                \
    c4_status_t state2 = fn2;                \
    if (state1 != C4_SUCCESS) return state1; \
    if (state2 != C4_SUCCESS) return state2; \
  } while (0)

/**
 * **TRY_ASYNC_FINAL(fn, final)** - Executes an async function and always runs a cleanup statement.
 *
 * The final statement is always executed regardless of whether the function
 * succeeds or fails. This is useful for cleanup operations that must happen
 * in all cases.
 *
 * @param fn Function call that returns c4_status_t
 * @param final Statement to always execute (e.g., cleanup code)
 *
 * ```c
 * TRY_ASYNC_FINAL(
 *   process_data(ctx, temp_buffer),
 *   safe_free(temp_buffer)
 * );
 * ```
 */
#define TRY_ASYNC_FINAL(fn, final)         \
  do {                                     \
    c4_status_t state = fn;                \
    final;                                 \
    if (state != C4_SUCCESS) return state; \
  } while (0)

/**
 * **TRY_ASYNC_CATCH(fn, cleanup)** - Executes an async function and runs cleanup only on failure.
 *
 * If the function returns anything other than C4_SUCCESS, the cleanup
 * statement is executed before returning the error status. This is useful
 * for error-path-only cleanup.
 *
 * @param fn Function call that returns c4_status_t
 * @param cleanup Statement to execute only on error
 *
 * ```c
 * TRY_ASYNC_CATCH(
 *   allocate_and_process(ctx),
 *   safe_free(ctx->temp_data)
 * );
 * ```
 */
#define TRY_ASYNC_CATCH(fn, cleanup) \
  do {                               \
    c4_status_t state = fn;          \
    if (state != C4_SUCCESS) {       \
      cleanup;                       \
      return state;                  \
    }                                \
  } while (0)

/**
 * Helper function to set the error message safely, handling memory ownership.
 * This avoids "use after free" issues in macros where the old error is read
 * while formulating the new one.
 */
static inline c4_status_t c4_state_set_error_msg(c4_state_t* state, char* msg) {
  if (state->error) safe_free(state->error);
  state->error = msg;
  return C4_ERROR;
}

/**
 * **THROW_ERROR(msg)** - Adds an error message to the state and returns C4_ERROR.
 *
 * This is a convenience macro for error handling that assumes a context
 * variable named 'ctx' with a 'state' field exists in scope.
 *
 * @param msg Error message string
 *
 * ```c
 * if (invalid_input) {
 *   THROW_ERROR("Invalid block number");
 * }
 * ```
 */
#define THROW_ERROR(msg) return c4_state_add_error(&ctx->state, msg)

/**
 * **THROW_ERROR_WITH(fmt, ...)** - Formats and adds an error message to the state and returns C4_ERROR.
 *
 * This macro allows printf-style formatting of error messages. The fmt parameter
 * MUST be a string literal (not a variable) due to compile-time string concatenation.
 * If fmt were a variable, compilation would fail, preventing format string vulnerabilities.
 *
 * @param fmt Format string (MUST be a string literal, e.g., "Error: %d")
 * @param ... Format arguments
 *
 * @warning The fmt parameter must be a compile-time string literal. Using a variable
 *          will result in a compilation error. This is a security feature.
 *
 * ```c
 * // CORRECT - string literal:
 * THROW_ERROR_WITH("Invalid block number: %u", block_num);
 *
 * // WRONG - will not compile:
 * char* msg = "Invalid block number: %u";
 * THROW_ERROR_WITH(msg, block_num);  // Compilation error!
 * ```
 */
#define THROW_ERROR_WITH(fmt, ...) \
  return c4_state_set_error_msg(&ctx->state, bprintf(NULL, "%s" fmt, ctx->state.error ? ctx->state.error : "", ##__VA_ARGS__))

/**
 * Static inline helpers for JSON validation macros to avoid static analyzer warnings.
 * These functions handle memory ownership of the validation error string.
 */
static inline c4_status_t c4_check_json_inline(c4_state_t* state, json_t val, const char* def, const char* prefix) {
  char* err = (char*) json_validate(val, def, prefix);
  if (err) return c4_state_set_error_msg(state, err);
  return C4_SUCCESS;
}

static inline c4_status_t c4_check_json_cached_inline(c4_state_t* state, json_t val, const char* def, const char* prefix) {
  char* err = (char*) json_validate_cached(val, def, prefix);
  if (err) return c4_state_set_error_msg(state, err);
  return C4_SUCCESS;
}

static inline bool c4_check_json_verify_inline(c4_state_t* state, bool* success, json_t val, const char* def, const char* prefix) {
  char* err = (char*) json_validate(val, def, prefix);
  if (err) {
    c4_state_set_error_msg(state, err);
    if (success) *success = false;
    return false;
  }
  return true;
}

static inline bool c4_check_json_verify_cached_inline(c4_state_t* state, bool* success, json_t val, const char* def, const char* prefix) {
  char* err = (char*) json_validate_cached(val, def, prefix);
  if (err) {
    c4_state_set_error_msg(state, err);
    if (success) *success = false;
    return false;
  }
  return true;
}

/**
 * **CHECK_JSON(val, def, error_prefix)** - Validates JSON data against a definition and returns on error.
 *
 * This macro validates JSON structure and returns C4_ERROR if validation fails.
 * Assumes a context variable named 'ctx' with a 'state' field exists in scope.
 *
 * @param val JSON value to validate
 * @param def JSON definition/schema to validate against
 * @param error_prefix String prefix for error messages
 *
 * ```c
 * CHECK_JSON(response_json, block_header_def, "Block header");
 * ```
 */
#define CHECK_JSON(val, def, error_prefix) \
  if (c4_check_json_inline(&ctx->state, val, def, error_prefix) != C4_SUCCESS) return C4_ERROR

/**
 * **CHECK_JSON_CACHED(val, def, error_prefix)** - Cached JSON validation for large payloads.
 *
 * Uses json_validate_cached() which skips validation if the same payload+schema
 * was recently validated successfully.
 */
#define CHECK_JSON_CACHED(val, def, error_prefix) \
  if (c4_check_json_cached_inline(&ctx->state, val, def, error_prefix) != C4_SUCCESS) return C4_ERROR

/**
 * **CHECK_JSON_VERIFY(val, def, error_prefix)** - Validates JSON data and sets verification failure on error.
 *
 * Similar to CHECK_JSON but used in verification context. Sets ctx->success
 * to false and returns false instead of C4_ERROR.
 *
 * @param val JSON value to validate
 * @param def JSON definition/schema to validate against
 * @param error_prefix String prefix for error messages
 *
 * ```c
 * CHECK_JSON_VERIFY(proof_json, proof_def, "Proof structure");
 * ```
 */
#define CHECK_JSON_VERIFY(val, def, error_prefix) \
  if (!c4_check_json_verify_inline(&ctx->state, &ctx->success, val, def, error_prefix)) return false

/**
 * **CHECK_JSON_VERIFY_CACHED(val, def, error_prefix)** - Cached variant for verification codepaths.
 */
#define CHECK_JSON_VERIFY_CACHED(val, def, error_prefix) \
  if (!c4_check_json_verify_cached_inline(&ctx->state, &ctx->success, val, def, error_prefix)) return false

/**
 * **RETRY_REQUEST(req)** - Marks current node as excluded and retries the request.
 *
 * This macro is used to retry a failed request with a different node.
 * It excludes the node that just responded by setting the corresponding
 * bit in the node_exclude_mask, clears the response, and returns C4_PENDING
 * to trigger a retry.
 *
 * @param req Pointer to the data_request_t to retry
 *
 * @note Only nodes with index < C4_MAX_NODES (16) can be excluded.
 *       Higher indices are ignored for safety.
 *
 * ```c
 * if (response_invalid) {
 *   RETRY_REQUEST(req);
 * }
 * ```
 */
#define RETRY_REQUEST(req)                                                                                                                                                                    \
  do {                                                                                                                                                                                        \
    if (req->response_node_index < C4_MAX_NODES)                                                                                                                                              \
      req->node_exclude_mask |= (1 << req->response_node_index);                                                                                                                              \
    log_warn("   [retry] request (%s) returned invalid response (%r) from node index=%d, retrying", c4_req_info(req->type, req->url, req->payload), req->response, req->response_node_index); \
    safe_free(req->response.data);                                                                                                                                                            \
    req->response = NULL_BYTES;                                                                                                                                                               \
    return C4_PENDING;                                                                                                                                                                        \
  } while (0)

#ifdef TEST
/**
 * Generates a mock filename for a request (test mode only).
 *
 * Creates a sanitized filename based on the request URL or payload,
 * suitable for storing mock responses. Characters that are invalid
 * in filenames are replaced with underscores.
 *
 * @param req Pointer to the data request
 * @return Allocated string with the mock filename (caller must free)
 *
 * **Note:** Only available when compiled with TEST flag.
 */
char* c4_req_mockname(data_request_t* req);
#endif

#ifdef __cplusplus
}
#endif

#endif
