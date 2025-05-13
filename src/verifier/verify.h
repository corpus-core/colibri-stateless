#ifndef verify_h__
#define verify_h__

#ifdef __cplusplus
extern "C" {
#endif

#define RPC_METHOD(name, data, proof) name
#include "../util/bytes.h"
#include "../util/chains.h"
#include "../util/crypto.h"
#include "../util/json.h"
#include "../util/ssz.h"
#include "../util/state.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// : APIs

// :: Internal APIs
//
// This header should be used only if you implement new verifiers or need direct access.
// The internal APIs are not guranteed to be stable and can be changed or removed without prior notice.

// ::: verify.h
// The verifier API execution a proof verification.
// when calling c4_verify_from_bytes, c4_verify needs to be called until the status is either C4_ERROR or C4_SUCCESS.
//
// Example:
//
// ```c
//  verify_ctx_t ctx = {0};
//  for (
//      c4_status_t status = c4_verify_from_bytes(&ctx, request, method, json_parse(args), chain_id);
//      status == C4_PENDING;
//      status = c4_verify(&ctx))
//      curl_fetch_all(&ctx.state);
//  if (ctx.success) {
//    ssz_dump_to_file_no_quotes(stdout, ctx.data);
//    return EXIT_SUCCESS;
//  }
//  else if (ctx.state.error) {
//    fprintf(stderr, "proof is invalid: %s\n", ctx.state.error);
//    return EXIT_FAILURE;
//  }
// ```
//

/**
 * a bitmask holding flags used during the verification context.
 */
typedef uint32_t verify_flags_t;

/**
 * a enum as list of flags used during the verification context.
 */
typedef enum {
  VERIFY_FLAG_FREE_DATA = 1 << 0, // if set, the data section will be freed after the verification, which is important, if the verifier extracts data from the request.
} verify_flag_t;

/**
 * a struct holding the verification context.
 */
typedef struct {
  char*          method;    // the rpc-method
  json_t         args;      // the rpc-args as json array
  ssz_ob_t       proof;     // the proof as ssz object using the Proof-Type directly
  ssz_ob_t       data;      // the data as ssz object or Empty if not needed
  ssz_ob_t       sync_data; // the sync-data as ssz object or Empty if not needed
  bool           success;   // true if the verification was successful
  c4_state_t     state;     // the state of the verification holding errors or data requests.
  chain_id_t     chain_id;  // the chain-id of the verification
  verify_flags_t flags;
} verify_ctx_t;

/**
 * a enum as list of method types.
 */
typedef enum {
  METHOD_UNDEFINED     = 0, // the method is not defined
  METHOD_PROOFABLE     = 1, // the method is proofable
  METHOD_UNPROOFABLE   = 2, // the method is unproofable
  METHOD_NOT_SUPPORTED = 3, // the method is not supported
  METHOD_LOCAL         = 4  // the method is executedlocal
} method_type_t;

/**
 * get the request type for a given chain-type.
 * For each chain-type there is one request-type used, the request-type will be specified by the verifier-module.
 */
const ssz_def_t* c4_get_request_type(chain_type_t chain_type);

/**
 * get the chain-type from a given request.
 *
 * The chain-type is based on the version-bytes of the request.
 */
chain_type_t c4_get_chain_type_from_req(bytes_t request);

/**
 * get the request type from a given request.
 *
 * The request-type is based on the version-bytes of the request.
 */
const ssz_def_t* c4_get_req_type_from_req(bytes_t request);

/**
 * the main verification function executionthe verifier in the modules.
 */
c4_status_t c4_verify(verify_ctx_t* ctx);

/**
 * shortcut to verify a request from bytes.
 * @param ctx the verification context.
 * @param request the request as bytes.
 * @param method the method to verify.
 * @param args the arguments for the method as json array.
 * @param chain_id the chain-id of the request.
 */
c4_status_t c4_verify_from_bytes(verify_ctx_t* ctx, bytes_t request, char* method, json_t args, chain_id_t chain_id);

/**
 * free all allocated memory from the verification context. it does not free the verification context itself.
 */
void c4_verify_free_data(verify_ctx_t* ctx);

/**
 * initialize the verification context.
 *
 * @param ctx the verification context.
 * @param request the request as bytes.
 * @param method the method to verify.
 * @param args the arguments for the method as json array.
 * @param chain_id the chain-id of the request.
 */
c4_status_t c4_verify_init(verify_ctx_t* ctx, bytes_t request, char* method, json_t args, chain_id_t chain_id);

/**
 * get the method type for a given chain-id and method.
 */
method_type_t c4_get_method_type(chain_id_t chain_id, char* method);

#pragma endregion
#ifdef MESSAGES
#define RETURN_VERIFY_ERROR(ctx, msg)     \
  do {                                    \
    c4_state_add_error(&ctx->state, msg); \
    ctx->success = false;                 \
    return false;                         \
  } while (0)
#else
#define RETURN_VERIFY_ERROR(ctx, msg) \
  do {                                \
    ctx->state.error = "E";           \
    return false;                     \
  } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif