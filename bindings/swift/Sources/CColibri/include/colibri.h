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

#include <stddef.h>
#include <stdint.h>

typedef void proofer_t;
#ifndef BYTES_T_DEFINED

typedef struct {
  uint32_t len;
  uint8_t* data;
} bytes_t;
#define BYTES_T_DEFINED
#endif
// : APIs

// :: Public Bindings API
//
// This header file is used within most bindings and provide all the core functions. If you link diectly with colibri, you should include the colibri.h header.

/**
 * creates a new proofer_ctx_t
 * @param method the method to prove (eth_getTransactionByHash, eth_getBlockByHash, etc.)
 * @param params the params of the method as jsons array like '["0x2af8e0202a3d4887781b4da03e6238f49f3a176835bc8c98525768d43af4aa24"]'
 * @param chain_id the chain id mainnet: 1, sepolia: 11155111, etc.
 * @param flags the flags to pass to the proofer ()
 * @return a new proofer_ctx_t
 */
proofer_t* c4_create_proofer_ctx(char* method, char* params, uint64_t chain_id, uint32_t flags);

/**
 * executes the proofer_t and returns the status as json string.
 * the resulting char* - ptr has to be freed by the caller.
 * The json-string has the following format:
 * ```json
 * {
 *  "status": "success" | "error" | "pending",
 *  "error?": "in case of error, the error message",
 *  "result?": "in case of success, the pointer to the bytes of the proof",
 *  "result_len?": "in case of success, the length of the proof",
 *  "requets?": [ // array of requests which need to be fetched before calling this function again.
 *    {
 *      "req_ptr": "pointer of the data request",
 *      "chain_id": "the chain id",
 *      "encoding": "the encoding of the request either json or ssz",
 *      "exclude_mask": "the exclude mask of the request indicating which endpoint to exclude",
 *      "method": "the method of the request (get, post, put, delete)",
 *      "url": "the url of the request",
 *      "payload?": "the payload of the request as json",
 *      "type": "the type of the request either beacon_api, eth_rpc or rest_api"
 *    }
 *  ]
 * }
 * ```
 */
char* c4_proofer_execute_json_status(proofer_t* ctx);

/**
 * gets the proof from the proofer_t
 * @param ctx the proofer_t
 * @return the proof as bytes_t
 */
bytes_t c4_proofer_get_proof(proofer_t* ctx);

/**
 * frees the proofer_ctx_t
 * @param ctx the proofer_ctx_t to free
 */
void c4_free_proofer_ctx(proofer_t* ctx);

/**
 * creates the response of the data request by allocating  memory where the data should be copied to.
 * @param req_ptr the pointer to the data request ( as given in the json-string of c4_proofer_execute_json_status)
 * @param len the length of the data to be set as response
 * @param node_index the  index of the node the response came from.
 * @return the pointer to the allocated memory
 */
void c4_req_set_response(void* req_ptr, bytes_t data, uint16_t node_index);

/**
 * sets the error of the data request
 * @param req_ptr the pointer to the data request ( as given in the json-string of c4_proofer_execute_json_status)
 * @param error the error message
 * @param node_index the  index of the node the error came from.
 */
void c4_req_set_error(void* req_ptr, char* error, uint16_t node_index);

/**
 * verifies the proof created by the proofer_ctx_t
 * @param proof the proof to verify
 * @param proof_len the length of the proof
 * @param method the method of the requested data
 * @param args the args as json array string
 * @param chain_id the chain id
 * @return the result of the verification as json string ( needs to be freed by the caller )
 */
void* c4_verify_create_ctx(bytes_t proof, char* method, char* args, uint64_t chain_id, char* trusted_block_hashes);

/**
 * verifies the proof created by the proofer_ctx_t and returns the result as json string.
 * @param ctx the context of the proof
 * @return the result of the verification as json string ( needs to be freed by the caller )
 */
char* c4_verify_execute_json_status(void* ctx);

/**
 * frees the verification context
 */
void c4_verify_free_ctx(void* ctx);

/**
 * gets the method type of a json-rpc-method
 * @param chain_id the chain id
 * @param method the method
 * @return the method type
 */
int c4_get_method_support(uint64_t chain_id, char* method);
