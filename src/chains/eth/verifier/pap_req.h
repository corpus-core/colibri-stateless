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

#ifndef pap_req_h__
#define pap_req_h__

#ifdef PAP

#include "verify.h"
#include "ssz.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * PAP request helpers for the verifier.
 *
 * These helpers mirror `c4_send_eth_rpc()` in the prover but operate on
 * `verify_ctx_t`. They encapsulate payload construction, request-ID
 * hashing, lookup / creation of `data_request_t`, and error propagation.
 *
 * Since they are compiled only with `PAP=ON` and called only from PAP
 * code paths, the linker will omit them from embedded builds.
 */

/**
 * Sends a proof request to the remote prover (POST, SSZ response).
 *
 * Builds the payload `{"method":"<method>","params":<params>}`,
 * computes the request ID via keccak, and either fills `out` with the
 * validated proof response or emits a new `C4_DATA_TYPE_PROVER` request.
 *
 * The returned `ssz_ob_t` has `def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST)`
 * and has been validated with `ssz_is_valid()` so all offsets are safe.
 *
 * @param ctx verification context
 * @param method RPC method name (e.g. `"eth_getBlockByNumber"`)
 * @param params JSON array string including brackets (e.g. `"[\"0x1a\",true]"`)
 * @param out receives the validated SSZ proof object on `C4_SUCCESS`
 * @return `C4_SUCCESS` when response is available, `C4_PENDING` when
 *         the request has been emitted, `C4_ERROR` on failure
 */
c4_status_t pap_request_proof(verify_ctx_t* ctx, const char* method, const char* params, ssz_ob_t* out);

/**
 * Sends a GET request to the remote prover server.
 *
 * @param ctx verification context
 * @param path URL path (e.g. `"/tx_cache"`)
 * @param ttl cache freshness hint in seconds (0 = no hint). Forwarded by the host as a
 *        `Cache-Control: max-age=<n>` request header so a shared cache/CDN never returns
 *        a response older than this bound.
 * @param out receives the response bytes on `C4_SUCCESS`
 * @return `C4_SUCCESS` when response is available, `C4_PENDING` when
 *         the request has been emitted, `C4_ERROR` on failure
 */
c4_status_t pap_request_get(verify_ctx_t* ctx, const char* path, uint32_t ttl, bytes_t* out);

/**
 * Sends a JSON-RPC request to the ETH execution client.
 *
 * Builds the full JSON-RPC envelope, computes the request ID via
 * keccak, and parses the `"result"` field from the response.
 *
 * If `json_check` is not NULL, the result is validated against the given
 * JSON schema string (same syntax as `json_validate`). The validation is
 * performed only once per request; subsequent calls skip it
 * (`data_request_t.validated`).
 *
 * @param ctx verification context
 * @param method RPC method name (e.g. `"eth_sendRawTransaction"`)
 * @param params JSON array string including brackets
 * @param json_check optional JSON validation schema for the result (NULL to skip)
 * @param out receives the parsed `result` json_t on `C4_SUCCESS`
 * @return `C4_SUCCESS` when result is available, `C4_PENDING` when
 *         the request has been emitted, `C4_ERROR` on failure
 */
c4_status_t pap_request_eth_rpc(verify_ctx_t* ctx, const char* method, const char* params, const char* json_check, json_t* out);

#ifdef __cplusplus
}
#endif

#endif /* PAP */
#endif /* pap_req_h__ */
