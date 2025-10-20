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

typedef void prover_t;
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
//
// This header file defines the core public API for Colibri's stateless Ethereum proof generation
// and verification system. It serves as the primary interface for language bindings (TypeScript,
// Python, Kotlin, Swift, etc.) and native C applications.
//
// ## Overview
//
// Colibri implements a **stateless light client** for Ethereum that can generate and verify
// cryptographic proofs for JSON-RPC method calls without maintaining blockchain state. The API
// provides:
//
// - **Proof Generation**: Create cryptographic proofs for Ethereum RPC calls
// - **Proof Verification**: Verify proofs and extract verified results
// - **Asynchronous Execution**: Non-blocking request handling with external data fetching
// - **Multi-Chain Support**: Works with Ethereum mainnet, testnets, and L2s
// - **Language Bindings**: Clean C API designed for easy FFI integration
//
// ## Architecture
//
// ### Stateless Design
//
// Unlike traditional Ethereum nodes that maintain the full blockchain state, Colibri operates
// **statelessly** by:
//
// 1. **Requesting only necessary data** from external sources (RPC nodes, beacon chain APIs)
// 2. **Building cryptographic proofs** from Merkle proofs and beacon chain sync committee signatures
// 3. **Verifying against trusted checkpoints** without storing any state
//
// This design allows Colibri to run efficiently in constrained environments (browsers, mobile devices,
// IoT) while providing the same security guarantees as a full node.
//
// ### Asynchronous Execution Model
//
// The API implements an asynchronous state machine where the C library cannot perform network I/O
// directly. Instead, the **host system** (JavaScript runtime, Python process, JVM, etc.) is responsible
// for executing HTTP requests.
//
// #### Execution Flow
//
// ```mermaid
// sequenceDiagram
//     participant Host as Host System<br/>(JS/Python/Kotlin/Swift)
//     participant Colibri as Colibri C Library
//     participant Network as External Data Sources<br/>(RPC/Beacon API)
//
//     Host->>Colibri: c4_create_prover_ctx(method, params, chain_id, flags)
//     Colibri-->>Host: prover_ctx_t*
//
//     loop Until Success or Error
//         Host->>Colibri: c4_prover_execute_json_status(ctx)
//         Colibri->>Colibri: Process available data
//
//         alt Data Required
//             Colibri-->>Host: {"status": "pending", "requests": [...]}
//
//             loop For each request
//                 Host->>Network: HTTP GET/POST (with retry logic)
//                 Network-->>Host: Response data or error
//                 Host->>Colibri: c4_req_set_response() or c4_req_set_error()
//             end
//
//         else Success
//             Colibri-->>Host: {"status": "success", "result": proof_ptr, "result_len": len}
//             Host->>Colibri: c4_prover_get_proof(ctx)
//             Colibri-->>Host: bytes_t proof
//
//         else Error
//             Colibri-->>Host: {"status": "error", "error": "error message"}
//         end
//     end
//
//     Host->>Colibri: c4_free_prover_ctx(ctx)
// ```
//
// This design allows the host system to use native async technologies (Promises in JavaScript,
// async/await in Python, coroutines in Kotlin) while keeping the C core synchronous and portable.
//
// ## Host System Responsibilities
//
// The host system (language binding or application using this API) has several critical responsibilities:
//
// ### 1. Configuration and Node Management
//
// The host system must maintain lists of data source endpoints for each chain:
//
// - **Ethereum RPC nodes** (`type: "eth_rpc"`): For execution layer data
// - **Beacon API nodes** (`type: "beacon_api"`): For consensus layer data
// - **Checkpointz servers** (`type: "checkpointz"`): For trusted sync committee checkpoints
// - **Prover servers** (`type: "prover"`): Optional centralized proof generation
//
// **Example Configuration:**
//
// ```javascript
// const config = {
//   chainId: 1, // Ethereum Mainnet
//   eth_rpcs: ["https://rpc.ankr.com/eth", "https://eth.llamarpc.com"],
//   beacon_apis: ["https://lodestar-mainnet.chainsafe.io"],
//   checkpointz: ["https://sync-mainnet.beaconcha.in"],
//   prover: ["https://mainnet.colibri-proof.tech"] // Optional
// };
// ```
//
// ### 2. Request Execution with Retry Logic
//
// When `c4_prover_execute_json_status()` or `c4_verify_execute_json_status()` returns `"status": "pending"`,
// the host system must execute all pending requests. For each request in the `"requests"` array:
//
// #### Step 1: Select Node
//
// Choose a node from the appropriate list based on `request.type`:
// - `"eth_rpc"` → use Ethereum RPC nodes
// - `"beacon_api"` → use Beacon API nodes
// - `"checkpointz"` → use Checkpointz servers
//
// #### Step 2: Apply Filters
//
// - **Skip excluded nodes**: If `(request.exclude_mask & (1 << node_index)) != 0`, skip this node
// - **Prefer client types**: If `request.preferred_client_type != 0`, prefer matching beacon clients
//
// #### Step 3: Build HTTP Request
//
// - **Method**: Use `request.method` (usually "GET" or "POST")
// - **URL**: Construct as `server_base_url + "/" + request.url`
// - **Headers**:
//   - `Content-Type`: `"application/json"` if `request.payload` is present
//   - `Accept`: `"application/octet-stream"` if `request.encoding == "ssz"`, else `"application/json"`
// - **Body**: Use `request.payload` as JSON for POST requests
//
// #### Step 4: Retry Logic
//
// If a request fails:
// 1. Try the next node in the list (respecting `exclude_mask`)
// 2. If all nodes fail, call `c4_req_set_error(request.req_ptr, error_message, 0)`
//
// If a request succeeds:
// 1. Call `c4_req_set_response(request.req_ptr, response_data, node_index)`
//
// #### Step 5: Parallel Execution
//
// For optimal performance, execute all pending requests **in parallel** using the host language's
// async capabilities (Promise.all, asyncio.gather, etc.).
//
// ### 3. Memory Management
//
// - **Allocated strings**: All strings returned from C functions (JSON status strings) must be freed by the host
// - **Context cleanup**: Always call `c4_free_prover_ctx()` or `c4_verify_free_ctx()` when done
// - **Request data**: Data passed to `c4_req_set_response()` is copied by the C library
// - **Error strings**: Error strings passed to `c4_req_set_error()` are copied by the C library
//
// ### 4. Error Handling
//
// The host system should handle:
// - **Network errors**: Retry with different nodes
// - **HTTP errors**: Check status codes, parse JSON-RPC error responses
// - **Timeouts**: Implement reasonable timeouts (30s recommended)
// - **Invalid responses**: Validate response format before calling `c4_req_set_response()`
//
// ## Data Request Structure
//
// When the status is `"pending"`, the `"requests"` array contains objects with:
//
// | Field | Type | Description |
// |-------|------|-------------|
// | `req_ptr` | number | Opaque pointer to pass to `c4_req_set_response()` or `c4_req_set_error()` |
// | `chain_id` | number | Chain ID for this request |
// | `type` | string | Request type: `"eth_rpc"`, `"beacon_api"`, `"checkpointz"`, `"rest_api"` |
// | `encoding` | string | Response encoding: `"json"` or `"ssz"` |
// | `method` | string | HTTP method: `"get"`, `"post"`, `"put"`, `"delete"` |
// | `url` | string | URL path to append to server base URL |
// | `payload` | object? | Optional JSON payload for POST/PUT requests |
// | `exclude_mask` | number | Bitmask of nodes to exclude (bit N = exclude node N) |
// | `preferred_client_type` | number | Preferred beacon client type (0 = any) |
//
// ## Method Support
//
// Not all Ethereum RPC methods can be proven. Use `c4_get_method_support()` to check:
//
// | Return Value | Enum | Meaning |
// |--------------|------|---------|
// | 1 | PROOFABLE | Method can be proven (use proof flow) |
// | 2 | UNPROOFABLE | Method exists but cannot be proven (call RPC directly) |
// | 3 | NOT_SUPPORTED | Method not supported by Colibri |
// | 4 | LOCAL | Method can be computed locally (no network needed) |
//
// **Proofable methods include:**
// - `eth_getBalance`, `eth_getCode`, `eth_getStorageAt`
// - `eth_getTransactionByHash`, `eth_getTransactionReceipt`
// - `eth_getBlockByHash`, `eth_getBlockByNumber`
// - `eth_getLogs`, `eth_call`, `eth_getProof`
//
// **Local methods include:**
// - `eth_chainId`, `net_version`
//
// ## Complete Usage Examples
//
// These examples show how to **call the C functions directly** from different host languages.
// This is useful for developers creating new language bindings or integrating Colibri into
// existing applications.
//
// {% tabs %}
// {% tab title="C" %}
// ```c
// #include "colibri.h"
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
//
// // Simple JSON parser helper (use a real JSON library in production)
// const char* get_json_string(const char* json, const char* key) {
//     // Simplified - use proper JSON library
//     return strstr(json, key);
// }
//
// void handle_requests(const char* status_json) {
//     // Parse requests array and handle each one
//     // This would use your HTTP client library
//     // Example: libcurl, platform HTTP API, etc.
// }
//
// int main() {
//     // Create prover context
//     prover_t* ctx = c4_create_prover_ctx(
//         "eth_getBalance",
//         "[\"0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045\", \"latest\"]",
//         1,    // Chain ID: Ethereum Mainnet
//         0     // Flags: no special options
//     );
//
//     if (!ctx) {
//         fprintf(stderr, "Failed to create prover context\n");
//         return 1;
//     }
//
//     // Execute proof generation loop
//     while (1) {
//         char* status_json = c4_prover_execute_json_status(ctx);
//
//         if (strstr(status_json, "\"status\": \"success\"")) {
//             // Success - get the proof
//             bytes_t proof = c4_prover_get_proof(ctx);
//             printf("Proof generated: %u bytes\n", proof.len);
//             // Use proof.data and proof.len...
//             free(status_json);
//             break;
//
//         } else if (strstr(status_json, "\"status\": \"error\"")) {
//             // Error occurred
//             fprintf(stderr, "Error: %s\n", status_json);
//             free(status_json);
//             break;
//
//         } else if (strstr(status_json, "\"status\": \"pending\"")) {
//             // Handle pending requests
//             handle_requests(status_json);
//             free(status_json);
//             // Loop continues...
//         }
//     }
//
//     // Cleanup
//     c4_free_prover_ctx(ctx);
//     return 0;
// }
// ```
// {% endtab %}
//
// {% tab title="TypeScript/JavaScript" %}
// ```typescript
// // Using Emscripten WASM bindings
// import { getC4w } from './wasm.js';
//
// async function createProof() {
//     const c4w = await getC4w();
//
//     // Allocate C strings from JS strings
//     const methodPtr = c4w._malloc(256);
//     const paramsPtr = c4w._malloc(1024);
//     c4w.stringToUTF8("eth_getBalance", methodPtr, 256);
//     c4w.stringToUTF8(
//         '["0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045", "latest"]',
//         paramsPtr,
//         1024
//     );
//
//     // Create prover context
//     const ctx = c4w._c4_create_prover_ctx(
//         methodPtr,
//         paramsPtr,
//         BigInt(1),  // Chain ID
//         0           // Flags
//     );
//
//     c4w._free(methodPtr);
//     c4w._free(paramsPtr);
//
//     if (!ctx) {
//         throw new Error("Failed to create prover context");
//     }
//
//     try {
//         // Execute proof generation loop
//         while (true) {
//             const statusPtr = c4w._c4_prover_execute_json_status(ctx);
//             const statusJson = c4w.UTF8ToString(statusPtr);
//             c4w._free(statusPtr);
//
//             const status = JSON.parse(statusJson);
//
//             if (status.status === "success") {
//                 // Get proof bytes
//                 const proofStruct = c4w._c4_prover_get_proof(ctx);
//                 const proofLen = c4w.getValue(proofStruct, 'i32');
//                 const proofDataPtr = c4w.getValue(proofStruct + 4, '*');
//                 const proof = new Uint8Array(
//                     c4w.HEAPU8.buffer,
//                     proofDataPtr,
//                     proofLen
//                 );
//                 console.log(`Proof generated: ${proof.length} bytes`);
//                 return proof;
//
//             } else if (status.status === "error") {
//                 throw new Error(`Proof generation failed: ${status.error}`);
//
//             } else if (status.status === "pending") {
//                 // Handle pending requests in parallel
//                 await Promise.all(status.requests.map(async (req) => {
//                     try {
//                         // Fetch data from network
//                         const response = await fetch(server + req.url, {
//                             method: req.method,
//                             body: req.payload ? JSON.stringify(req.payload) : undefined,
//                             headers: {
//                                 "Accept": req.encoding === "ssz"
//                                     ? "application/octet-stream"
//                                     : "application/json"
//                             }
//                         });
//
//                         const data = new Uint8Array(await response.arrayBuffer());
//
//                         // Copy data to C memory
//                         const dataPtr = c4w._malloc(data.length);
//                         c4w.HEAPU8.set(data, dataPtr);
//
//                         // Set response
//                         c4w._c4_req_set_response(
//                             req.req_ptr,
//                             dataPtr,
//                             data.length,
//                             0  // node_index
//                         );
//
//                         c4w._free(dataPtr);
//
//                     } catch (error) {
//                         // Set error
//                         const errorStr = error.message;
//                         const errorPtr = c4w._malloc(errorStr.length + 1);
//                         c4w.stringToUTF8(errorStr, errorPtr, errorStr.length + 1);
//                         c4w._c4_req_set_error(req.req_ptr, errorPtr, 0);
//                         c4w._free(errorPtr);
//                     }
//                 }));
//             }
//         }
//     } finally {
//         // Cleanup
//         c4w._c4_free_prover_ctx(ctx);
//     }
// }
// ```
// {% endtab %}
//
// {% tab title="Python" %}
// ```python
// import ctypes
// import json
// from typing import Dict, Any
//
// # Load the native library
// lib = ctypes.CDLL('./libcolibri.so')
//
// # Define C types
// class BytesT(ctypes.Structure):
//     _fields_ = [
//         ("len", ctypes.c_uint32),
//         ("data", ctypes.POINTER(ctypes.c_uint8))
//     ]
//
// # Define function signatures
// lib.c4_create_prover_ctx.argtypes = [
//     ctypes.c_char_p,  # method
//     ctypes.c_char_p,  # params
//     ctypes.c_uint64,  # chain_id
//     ctypes.c_uint32   # flags
// ]
// lib.c4_create_prover_ctx.restype = ctypes.c_void_p
//
// lib.c4_prover_execute_json_status.argtypes = [ctypes.c_void_p]
// lib.c4_prover_execute_json_status.restype = ctypes.c_char_p
//
// lib.c4_prover_get_proof.argtypes = [ctypes.c_void_p]
// lib.c4_prover_get_proof.restype = BytesT
//
// lib.c4_req_set_response.argtypes = [
//     ctypes.c_void_p,  # req_ptr
//     BytesT,           # data
//     ctypes.c_uint16   # node_index
// ]
//
// lib.c4_req_set_error.argtypes = [
//     ctypes.c_void_p,  # req_ptr
//     ctypes.c_char_p,  # error
//     ctypes.c_uint16   # node_index
// ]
//
// lib.c4_free_prover_ctx.argtypes = [ctypes.c_void_p]
//
// async def create_proof():
//     # Create prover context
//     ctx = lib.c4_create_prover_ctx(
//         b"eth_getBalance",
//         b'["0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045", "latest"]',
//         1,  # Chain ID
//         0   # Flags
//     )
//
//     if not ctx:
//         raise Exception("Failed to create prover context")
//
//     try:
//         # Execute proof generation loop
//         while True:
//             status_json = lib.c4_prover_execute_json_status(ctx)
//             status = json.loads(status_json.decode('utf-8'))
//
//             if status["status"] == "success":
//                 # Get proof bytes
//                 proof_struct = lib.c4_prover_get_proof(ctx)
//                 proof_bytes = bytes(proof_struct.data[:proof_struct.len])
//                 print(f"Proof generated: {len(proof_bytes)} bytes")
//                 return proof_bytes
//
//             elif status["status"] == "error":
//                 raise Exception(f"Proof generation failed: {status['error']}")
//
//             elif status["status"] == "pending":
//                 # Handle pending requests
//                 for req in status["requests"]:
//                     try:
//                         # Fetch data from network
//                         response = await fetch_data(req)
//
//                         # Create bytes_t structure
//                         data_array = (ctypes.c_uint8 * len(response))(*response)
//                         bytes_t = BytesT(len=len(response), data=data_array)
//
//                         # Set response
//                         lib.c4_req_set_response(
//                             req["req_ptr"],
//                             bytes_t,
//                             0  # node_index
//                         )
//
//                     except Exception as e:
//                         # Set error
//                         lib.c4_req_set_error(
//                             req["req_ptr"],
//                             str(e).encode('utf-8'),
//                             0
//                         )
//     finally:
//         # Cleanup
//         lib.c4_free_prover_ctx(ctx)
// ```
// {% endtab %}
//
// {% tab title="Kotlin/JVM" %}
// ```kotlin
// import com.corpuscore.colibri.c4
// import org.json.JSONObject
//
// suspend fun createProof(): ByteArray {
//     // Create prover context via JNI
//     val ctx = c4.c4_create_prover_ctx(
//         "eth_getBalance",
//         "[\"0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045\", \"latest\"]",
//         BigInteger.ONE,  // Chain ID
//         0                // Flags
//     ) ?: throw Exception("Failed to create prover context")
//
//     try {
//         // Execute proof generation loop
//         while (true) {
//             val statusJson = c4.c4_prover_execute_json_status(ctx)
//             val status = JSONObject(statusJson)
//
//             when (status.getString("status")) {
//                 "success" -> {
//                     // Get proof bytes
//                     val proof = c4.c4_prover_get_proof(ctx)
//                     println("Proof generated: ${proof.size} bytes")
//                     return proof
//                 }
//
//                 "error" -> {
//                     val error = status.getString("error")
//                     throw Exception("Proof generation failed: $error")
//                 }
//
//                 "pending" -> {
//                     // Handle pending requests in parallel
//                     val requests = status.getJSONArray("requests")
//                     for (i in 0 until requests.length()) {
//                         val req = requests.getJSONObject(i)
//                         val reqPtr = req.getLong("req_ptr")
//
//                         try {
//                             // Fetch data from network
//                             val response = fetchData(req)
//
//                             // Set response via JNI
//                             c4.c4_req_set_response(
//                                 reqPtr,
//                                 response,
//                                 0  // node_index
//                             )
//
//                         } catch (e: Exception) {
//                             // Set error via JNI
//                             c4.c4_req_set_error(
//                                 reqPtr,
//                                 e.message ?: "Unknown error",
//                                 0
//                             )
//                         }
//                     }
//                 }
//             }
//         }
//     } finally {
//         // Cleanup
//         c4.c4_free_prover_ctx(ctx)
//     }
// }
// ```
// {% endtab %}
//
// {% tab title="Swift" %}
// ```swift
// import Foundation
// import CColibri  // C module
//
// func createProof() async throws -> Data {
//     // Create C strings
//     let methodCStr = strdup("eth_getBalance")
//     let paramsCStr = strdup(
//         "[\"0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045\", \"latest\"]"
//     )
//
//     defer {
//         free(methodCStr)
//         free(paramsCStr)
//     }
//
//     // Create prover context
//     guard let ctx = c4_create_prover_ctx(
//         methodCStr,
//         paramsCStr,
//         1,  // Chain ID
//         0   // Flags
//     ) else {
//         throw ColibriError.contextCreationFailed
//     }
//
//     defer {
//         c4_free_prover_ctx(ctx)
//     }
//
//     // Execute proof generation loop
//     while true {
//         guard let statusPtr = c4_prover_execute_json_status(ctx) else {
//             throw ColibriError.nullPointerReceived
//         }
//
//         let statusJson = String(cString: statusPtr)
//         free(statusPtr)
//
//         guard let statusData = statusJson.data(using: .utf8),
//               let status = try? JSONSerialization.jsonObject(with: statusData) as? [String: Any],
//               let statusStr = status["status"] as? String else {
//             throw ColibriError.invalidJSON
//         }
//
//         switch statusStr {
//         case "success":
//             // Get proof bytes
//             let proof = c4_prover_get_proof(ctx)
//             let proofData = Data(
//                 bytes: proof.data,
//                 count: Int(proof.len)
//             )
//             print("Proof generated: \(proofData.count) bytes")
//             return proofData
//
//         case "error":
//             let errorMsg = status["error"] as? String ?? "Unknown error"
//             throw ColibriError.proofError(errorMsg)
//
//         case "pending":
//             // Handle pending requests
//             guard let requests = status["requests"] as? [[String: Any]] else {
//                 throw ColibriError.invalidJSON
//             }
//
//             for request in requests {
//                 guard let reqPtrNum = request["req_ptr"] as? NSNumber else {
//                     continue
//                 }
//                 let reqPtr = UnsafeMutableRawPointer(
//                     bitPattern: UInt(reqPtrNum.int64Value)
//                 )
//
//                 do {
//                     // Fetch data from network
//                     let responseData = try await fetchData(request)
//
//                     // Create bytes_t structure
//                     let bytes = responseData.withUnsafeBytes { rawBuffer in
//                         bytes_t(
//                             len: UInt32(responseData.count),
//                             data: UnsafeMutablePointer(
//                                 mutating: rawBuffer.bindMemory(to: UInt8.self).baseAddress!
//                             )
//                         )
//                     }
//
//                     // Set response
//                     c4_req_set_response(reqPtr, bytes, 0)
//
//                 } catch {
//                     // Set error
//                     let errorStr = error.localizedDescription
//                     let errorPtr = strdup(errorStr)
//                     c4_req_set_error(reqPtr, errorPtr, 0)
//                     free(errorPtr)
//                 }
//             }
//
//         default:
//             throw ColibriError.unknownStatus(statusStr)
//         }
//     }
// }
// ```
// {% endtab %}
// {% endtabs %}
//
// **Note**: These examples are simplified for clarity. In production code, you should:
// - Use proper JSON parsing libraries (not string matching)
// - Implement robust error handling and retry logic
// - Handle memory management carefully (especially in C/manual memory languages)
// - Execute pending requests in parallel for optimal performance
// - Implement proper node selection and exclusion mask handling
//
// ## Best Practices
//
// ### Performance Optimization
//
// 1. **Parallel Request Execution**: Always execute pending requests in parallel
// 2. **Connection Pooling**: Reuse HTTP connections across requests
// 3. **Response Caching**: Cache responses based on request URL and payload (respect TTL)
// 4. **Node Selection**: Track node reliability and prefer faster/more reliable nodes
//
// ### Error Handling
//
// 1. **Retry with Backoff**: Implement exponential backoff for transient errors
// 2. **Fallback Nodes**: Always configure multiple nodes per data source type
// 3. **Timeout Handling**: Use reasonable timeouts (30s recommended)
// 4. **Graceful Degradation**: Fall back to direct RPC for UNPROOFABLE methods
//
// ### Security Considerations
//
// 1. **Trusted Checkpoint**: Always configure a trusted checkpoint for initial sync
// 2. **Node Diversity**: Use nodes from different operators to prevent eclipse attacks
// 3. **Response Validation**: Validate response formats before passing to C library
// 4. **Memory Safety**: Always free returned strings and contexts
//

/**
 * Creates a new prover context for generating a proof.
 *
 * This function initializes the proof generation process for a specific Ethereum RPC method.
 * The returned context must be used with `c4_prover_execute_json_status()` to drive the
 * asynchronous proof generation process.
 *
 * **Memory Management**: The caller is responsible for freeing the returned context using
 * `c4_free_prover_ctx()` when done.
 *
 * @param method The Ethereum RPC method to prove (e.g., "eth_getBalance", "eth_getBlockByHash")
 * @param params The method parameters as a JSON array string (e.g., '["0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045", "latest"]')
 * @param chain_id The blockchain chain ID (1 = Ethereum Mainnet, 11155111 = Sepolia, etc.)
 * @param flags Flags to customize proof generation:
 *              - Bit 0 (0x01): Include contract code in proof
 *              - Other bits reserved for future use
 * @return A new prover context pointer, or NULL if creation failed
 *
 * **Example**:
 * ```c
 * prover_t* ctx = c4_create_prover_ctx(
 *     "eth_getBalance",
 *     "[\"0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045\", \"latest\"]",
 *     1,
 *     0
 * );
 * if (!ctx) {
 *     fprintf(stderr, "Failed to create prover context\n");
 *     return -1;
 * }
 * ```
 */
prover_t* c4_create_prover_ctx(char* method, char* params, uint64_t chain_id, uint32_t flags);

/**
 * Executes one step of the proof generation state machine.
 *
 * This function drives the asynchronous proof generation process. Call it repeatedly in a loop
 * until it returns a status of "success" or "error". When it returns "pending", the host system
 * must handle the pending data requests before calling this function again.
 *
 * **Memory Management**: The returned JSON string must be freed by the caller using `free()`.
 *
 * @param ctx The prover context created by `c4_create_prover_ctx()`
 * @return A JSON string describing the current status (see format below)
 *
 * **Return Value Format**:
 *
 * The function returns a JSON string with one of three possible statuses:
 *
 * **Success** (proof generation complete):
 * ```json
 * {
 *   "status": "success",
 *   "result": "0x7ffe1234abcd",
 *   "result_len": 1024
 * }
 * ```
 * - `result`: Hexadecimal string pointer to proof data (use with `c4_prover_get_proof()`)
 * - `result_len`: Length of the proof in bytes
 *
 * **Error** (proof generation failed):
 * ```json
 * {
 *   "status": "error",
 *   "error": "Failed to fetch block header: connection timeout"
 * }
 * ```
 * - `error`: Human-readable error message
 *
 * **Pending** (waiting for external data):
 * ```json
 * {
 *   "status": "pending",
 *   "requests": [
 *     {
 *       "req_ptr": 140736471234560,
 *       "chain_id": 1,
 *       "type": "beacon_api",
 *       "encoding": "json",
 *       "method": "get",
 *       "url": "eth/v1/beacon/light_client/finality_update",
 *       "exclude_mask": 0
 *     },
 *     {
 *       "req_ptr": 140736471234688,
 *       "chain_id": 1,
 *       "type": "eth_rpc",
 *       "encoding": "json",
 *       "method": "post",
 *       "url": "",
 *       "payload": {
 *         "method": "eth_getBlockByNumber",
 *         "params": ["0x1234", false]
 *       },
 *       "exclude_mask": 0
 *     }
 *   ]
 * }
 * ```
 * - `requests`: Array of data requests that must be fulfilled (see "Data Request Structure" in API docs)
 *
 * **Example Usage**:
 * ```c
 * prover_t* ctx = c4_create_prover_ctx("eth_getBalance", "[\"0xabc...\", \"latest\"]", 1, 0);
 *
 * while (1) {
 *     char* status_json = c4_prover_execute_json_status(ctx);
 *
 *     // Parse JSON (use your favorite JSON parser)
 *     json_t status = parse_json(status_json);
 *     free(status_json);
 *
 *     if (strcmp(status.status, "success") == 0) {
 *         bytes_t proof = c4_prover_get_proof(ctx);
 *         // Use the proof...
 *         break;
 *     } else if (strcmp(status.status, "error") == 0) {
 *         fprintf(stderr, "Error: %s\n", status.error);
 *         break;
 *     } else if (strcmp(status.status, "pending") == 0) {
 *         // Handle pending requests (see Host System Responsibilities)
 *         for (int i = 0; i < status.requests_count; i++) {
 *             handle_request(&status.requests[i]);
 *         }
 *     }
 * }
 *
 * c4_free_prover_ctx(ctx);
 * ```
 */
char* c4_prover_execute_json_status(prover_t* ctx);

/**
 * Retrieves the generated proof from a completed prover context.
 *
 * This function should only be called after `c4_prover_execute_json_status()` returns
 * a status of "success". The proof data is owned by the context and remains valid until
 * `c4_free_prover_ctx()` is called.
 *
 * **Memory Management**: The returned bytes_t.data pointer is owned by the context.
 * Do NOT call `free()` on it. Copy the data if you need to retain it after freeing the context.
 *
 * @param ctx The prover context (must be in "success" state)
 * @return A bytes_t structure containing the proof data
 *
 * **Example**:
 * ```c
 * // After successful proof generation
 * bytes_t proof = c4_prover_get_proof(ctx);
 *
 * // Copy proof data for later use
 * uint8_t* proof_copy = malloc(proof.len);
 * memcpy(proof_copy, proof.data, proof.len);
 *
 * // Now safe to free context
 * c4_free_prover_ctx(ctx);
 *
 * // Use proof_copy...
 * free(proof_copy);
 * ```
 */
bytes_t c4_prover_get_proof(prover_t* ctx);

/**
 * Frees all resources associated with a prover context.
 *
 * This function must be called to clean up a prover context created by `c4_create_prover_ctx()`.
 * After calling this function, the context pointer is invalid and must not be used.
 *
 * **Memory Management**: This frees the context and all associated internal memory, including
 * the proof data. If you need the proof data after freeing the context, copy it first using
 * `c4_prover_get_proof()`.
 *
 * @param ctx The prover context to free (may be NULL, in which case this is a no-op)
 *
 * **Example**:
 * ```c
 * prover_t* ctx = c4_create_prover_ctx(...);
 * // ... use context ...
 * c4_free_prover_ctx(ctx);
 * ctx = NULL; // Good practice to avoid use-after-free
 * ```
 */
void c4_free_prover_ctx(prover_t* ctx);

/**
 * Sets the successful response data for a pending data request.
 *
 * When `c4_prover_execute_json_status()` or `c4_verify_execute_json_status()` returns
 * `"status": "pending"`, the host system must fetch the data for each request and call
 * this function to provide the response. After all pending requests are fulfilled, call
 * the execute function again to continue processing.
 *
 * **Memory Management**: The data is **copied** by this function. The caller retains ownership
 * of the input data and is responsible for freeing it if needed.
 *
 * @param req_ptr Opaque request pointer from the "req_ptr" field in the JSON status
 * @param data The response data as bytes_t (will be copied)
 * @param node_index Index of the node that provided this response (0-15, used for exclude_mask)
 *
 * **Example**:
 * ```c
 * // Parse pending requests from JSON status
 * for (each request in status.requests) {
 *     void* req_ptr = request.req_ptr;
 *
 *     // Fetch data from network
 *     uint8_t* response_data = fetch_url(request.url);
 *     size_t response_len = get_response_length();
 *
 *     // Set response (data is copied, so safe to free after)
 *     bytes_t response = { .len = response_len, .data = response_data };
 *     c4_req_set_response(req_ptr, response, node_index);
 *
 *     free(response_data); // Safe to free immediately
 * }
 * ```
 */
void c4_req_set_response(void* req_ptr, bytes_t data, uint16_t node_index);

/**
 * Sets an error for a pending data request.
 *
 * When a data request cannot be fulfilled (network error, all nodes failed, invalid response),
 * call this function to report the error. The proof generation/verification will typically
 * fail with this error message, or may retry with different parameters.
 *
 * **Memory Management**: The error string is **copied** by this function. The caller retains
 * ownership and is responsible for freeing it if needed.
 *
 * @param req_ptr Opaque request pointer from the "req_ptr" field in the JSON status
 * @param error Error message string (will be copied)
 * @param node_index Index of the node that failed (0-15), or 0 if all nodes failed
 *
 * **Example**:
 * ```c
 * // Try to fetch from multiple nodes
 * char* last_error = NULL;
 * for (int i = 0; i < num_nodes; i++) {
 *     if (request.exclude_mask & (1 << i)) continue; // Skip excluded nodes
 *
 *     if (try_fetch(nodes[i], request.url, &response)) {
 *         c4_req_set_response(req_ptr, response, i);
 *         return; // Success
 *     } else {
 *         last_error = get_last_error();
 *     }
 * }
 *
 * // All nodes failed
 * c4_req_set_error(req_ptr, last_error ? last_error : "All nodes failed", 0);
 * ```
 */
void c4_req_set_error(void* req_ptr, char* error, uint16_t node_index);

/**
 * Creates a verification context for verifying a proof.
 *
 * This function initializes the proof verification process. The returned context must be
 * used with `c4_verify_execute_json_status()` to drive the asynchronous verification process.
 *
 * Verification may require additional data from external sources (e.g., beacon chain finality
 * updates, sync committee data), so it follows the same asynchronous execution model as
 * proof generation.
 *
 * **Memory Management**: The caller is responsible for freeing the returned context using
 * `c4_verify_free_ctx()` when done.
 *
 * @param proof The proof data to verify
 * @param method The Ethereum RPC method that was proven (e.g., "eth_getBalance")
 * @param args The method arguments as JSON array string (must match proof)
 * @param chain_id The blockchain chain ID (must match proof)
 * @param trusted_checkpoint Optional trusted checkpoint as hex string (0x-prefixed, 66 chars),
 *                           or NULL/empty string to use the default checkpoint for this chain
 * @return A new verification context pointer, or NULL if creation failed
 *
 * **Trusted Checkpoints**:
 *
 * A trusted checkpoint is a 32-byte hash of a beacon chain block root that is assumed to be
 * correct. Colibri uses this checkpoint as the starting point for verification, avoiding the
 * need to sync from genesis.
 *
 * - **Format**: `"0x" + 64 hex characters` (66 characters total)
 * - **Recommended**: Use a recent finalized beacon block root from a trusted source
 * - **If NULL**: Uses the built-in checkpoint for the chain (may be outdated)
 *
 * **Example**:
 * ```c
 * // Verify a proof with a trusted checkpoint
 * bytes_t proof = { .len = proof_len, .data = proof_data };
 * void* ctx = c4_verify_create_ctx(
 *     proof,
 *     "eth_getBalance",
 *     "[\"0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045\", \"latest\"]",
 *     1, // Ethereum Mainnet
 *     "0x1234567890abcdef..." // Trusted checkpoint (66 chars)
 * );
 *
 * if (!ctx) {
 *     fprintf(stderr, "Failed to create verification context\n");
 *     return -1;
 * }
 * ```
 */
void* c4_verify_create_ctx(bytes_t proof, char* method, char* args, uint64_t chain_id, char* trusted_checkpoint);

/**
 * Executes one step of the proof verification state machine.
 *
 * This function drives the asynchronous proof verification process. Call it repeatedly in a loop
 * until it returns a status of "success" or "error". When it returns "pending", the host system
 * must handle the pending data requests before calling this function again.
 *
 * The execution flow is identical to `c4_prover_execute_json_status()`, but returns the verified
 * result instead of a proof.
 *
 * **Memory Management**: The returned JSON string must be freed by the caller using `free()`.
 *
 * @param ctx The verification context created by `c4_verify_create_ctx()`
 * @return A JSON string describing the current status (see format below)
 *
 * **Return Value Format**:
 *
 * **Success** (verification complete):
 * ```json
 * {
 *   "status": "success",
 *   "result": {
 *     // The verified RPC result (format depends on method)
 *     // For eth_getBalance: "0x1234567890abcdef"
 *     // For eth_getBlockByNumber: { number: "0x1234", hash: "0xabcd...", ... }
 *   }
 * }
 * ```
 *
 * **Error** (verification failed):
 * ```json
 * {
 *   "status": "error",
 *   "error": "Invalid proof: Merkle root mismatch"
 * }
 * ```
 *
 * **Pending** (waiting for external data):
 * ```json
 * {
 *   "status": "pending",
 *   "requests": [
 *     // Same format as c4_prover_execute_json_status()
 *   ]
 * }
 * ```
 *
 * **Example Usage**:
 * ```c
 * void* ctx = c4_verify_create_ctx(proof, method, args, chain_id, checkpoint);
 *
 * while (1) {
 *     char* status_json = c4_verify_execute_json_status(ctx);
 *
 *     json_t status = parse_json(status_json);
 *     free(status_json);
 *
 *     if (strcmp(status.status, "success") == 0) {
 *         printf("Verified result: %s\n", json_stringify(status.result));
 *         break;
 *     } else if (strcmp(status.status, "error") == 0) {
 *         fprintf(stderr, "Verification failed: %s\n", status.error);
 *         break;
 *     } else if (strcmp(status.status, "pending") == 0) {
 *         for (int i = 0; i < status.requests_count; i++) {
 *             handle_request(&status.requests[i]);
 *         }
 *     }
 * }
 *
 * c4_verify_free_ctx(ctx);
 * ```
 */
char* c4_verify_execute_json_status(void* ctx);

/**
 * Frees all resources associated with a verification context.
 *
 * This function must be called to clean up a verification context created by
 * `c4_verify_create_ctx()`. After calling this function, the context pointer is
 * invalid and must not be used.
 *
 * @param ctx The verification context to free (may be NULL, in which case this is a no-op)
 *
 * **Example**:
 * ```c
 * void* ctx = c4_verify_create_ctx(...);
 * // ... use context ...
 * c4_verify_free_ctx(ctx);
 * ctx = NULL; // Good practice
 * ```
 */
void c4_verify_free_ctx(void* ctx);

/**
 * Queries whether a specific RPC method is supported and how it should be handled.
 *
 * Not all Ethereum RPC methods can be cryptographically proven. This function returns
 * information about method support, allowing the host system to decide how to handle
 * each RPC call.
 *
 * @param chain_id The blockchain chain ID to check
 * @param method The Ethereum RPC method name (e.g., "eth_getBalance")
 * @return Method support type (see table below)
 *
 * **Return Values**:
 *
 * | Value | Name | Meaning | How to Handle |
 * |-------|------|---------|---------------|
 * | 1 | PROOFABLE | Method can be cryptographically proven | Use proof generation and verification flow |
 * | 2 | UNPROOFABLE | Method exists but cannot be proven | Call RPC node directly without proof |
 * | 3 | NOT_SUPPORTED | Method is not supported by Colibri | Return error to caller |
 * | 4 | LOCAL | Method can be computed locally | Use verification with empty proof |
 * | 0 | UNKNOWN | Unknown method or error | Treat as NOT_SUPPORTED |
 *
 * **Proofable Methods** (return 1):
 * - State queries: `eth_getBalance`, `eth_getCode`, `eth_getStorageAt`, `eth_getProof`
 * - Transaction queries: `eth_getTransactionByHash`, `eth_getTransactionReceipt`, `eth_getTransactionByBlockHashAndIndex`, `eth_getTransactionByBlockNumberAndIndex`
 * - Block queries: `eth_getBlockByHash`, `eth_getBlockByNumber`, `eth_getBlockTransactionCountByHash`, `eth_getBlockTransactionCountByNumber`
 * - Log queries: `eth_getLogs`
 * - Call simulation: `eth_call`, `eth_estimateGas`
 *
 * **Unproofable Methods** (return 2):
 * - Mempool: `eth_sendTransaction`, `eth_sendRawTransaction`, `eth_getTransactionCount` (pending)
 * - Mining: `eth_mining`, `eth_hashrate`, `eth_getWork`, `eth_submitWork`
 * - Network: `net_listening`, `net_peerCount`
 *
 * **Local Methods** (return 4):
 * - `eth_chainId`: Returns the configured chain ID
 * - `net_version`: Returns the network version (same as chain ID)
 *
 * **Example**:
 * ```c
 * int support = c4_get_method_support(1, "eth_getBalance");
 *
 * switch (support) {
 *     case 1: // PROOFABLE
 *         printf("Creating proof for eth_getBalance...\n");
 *         // Use c4_create_prover_ctx() + c4_verify_create_ctx()
 *         break;
 *     case 2: // UNPROOFABLE
 *         printf("Calling eth_getBalance directly on RPC...\n");
 *         // Make HTTP request to RPC node
 *         break;
 *     case 3: // NOT_SUPPORTED
 *         printf("Method eth_getBalance not supported\n");
 *         return error;
 *     case 4: // LOCAL
 *         printf("Computing eth_chainId locally...\n");
 *         // Use c4_verify_create_ctx() with empty proof
 *         break;
 * }
 * ```
 */
int c4_get_method_support(uint64_t chain_id, char* method);
