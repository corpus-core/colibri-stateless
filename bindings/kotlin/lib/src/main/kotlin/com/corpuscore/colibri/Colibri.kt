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

package com.corpuscore.colibri

// Import the SWIG-generated class explicitly
//import c4
import kotlin.concurrent.withLock
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import org.json.JSONArray
import java.math.BigInteger
import io.ktor.client.*
import io.ktor.client.engine.cio.*
import io.ktor.client.request.*
import io.ktor.client.statement.*
import io.ktor.http.*

// Define MethodType Enum
enum class MethodType(val value: Int) {
    PROOFABLE(1),
    UNPROOFABLE(2),
    NOT_SUPPORTED(3),
    LOCAL(4),
    UNKNOWN(0); // Default/unknown case

    companion object {
        fun fromInt(value: Int) = entries.find { it.value == value } ?: UNKNOWN
    }
}

/** Pragmatic Adaptive Privacy mode. BASIC sets verify flag for PAP. */
enum class PrivacyMode {
    NONE,
    BASIC
}

enum class ProverMode(val value: Int) {
    LOCAL(0),
    REMOTE(1),
    HYBRID(2),
    PROXY(3),
    LIGHT_CLIENT(4)
}

// Custom Exception for Colibri errors
open class ColibriException(message: String) : RuntimeException(message)

/**
 * Thrown when an `eth_call` (or similar EVM execution) ran to completion but
 * reverted. The verifier has fully verified the revert -- this is a legitimate
 * EVM outcome, not a transport or proof error.
 *
 * Maps to the Geth-style RPC error `{ "code": 3, "message": "execution reverted",
 * "data": "0x..." }`, which is also the EIP-1193 representation used by ethers
 * to decode `OffchainLookup` (EIP-3668 / CCIP-Read) and custom Solidity errors.
 *
 * @param data Raw EVM revert return-data as `0x`-prefixed hex string
 *   (`"0x"` when empty). Callers typically ABI-decode this with the
 *   contract's error definitions.
 */
class ColibriRevertException(val data: String) : ColibriException("execution reverted") {
    /** EIP-1193 / Geth RPC error code for `execution reverted`. */
    val code: Int = 3
}

// Interface for storage operations callback.
// Implementations MUST be thread-safe when used with coroutines on Dispatchers.IO.
interface ColibriStorage {
    fun get(key: String): ByteArray?
    fun set(key: String, value: ByteArray)
    fun delete(key: String)
}

/**
 * Thread-safe wrapper around any [ColibriStorage] implementation.
 * Serializes all operations with a [ReentrantLock] so the underlying
 * delegate does not need to be thread-safe itself.
 */
class ThreadSafeStorage(private val delegate: ColibriStorage) : ColibriStorage {
    private val lock = java.util.concurrent.locks.ReentrantLock()
    override fun get(key: String): ByteArray? = lock.withLock { delegate.get(key) }
    override fun set(key: String, value: ByteArray) = lock.withLock { delegate.set(key, value) }
    override fun delete(key: String) = lock.withLock { delegate.delete(key) }
}

// Singleton object to hold the user-provided storage implementation
object StorageBridge {
    var implementation: ColibriStorage? = null
}

// Type alias for the request handler callback
typealias RequestHandler = (requestDetails: Map<String, Any?>) -> ByteArray?

class Colibri(
    var chainId: BigInteger = BigInteger.ONE, // Default value
    var provers: Array<String> = arrayOf("https://c4.incubed.net"), // Default value
    var ethRpcs: Array<String> = arrayOf("https://rpc.ankr.com/eth"), // Default value
    var beaconApis: Array<String> = arrayOf("https://lodestar-mainnet.chainsafe.io"), // Default value
    var checkpointz: Array<String> = arrayOf("https://sync-mainnet.beaconcha.in", "https://beaconstate.info", "https://sync.invis.tools", "https://beaconstate.ethstaker.cc"), // Default checkpointz servers
    var obliviousNodes: Array<String> = emptyArray(), // TEE RPC endpoints for eth_getProof
    var trustedCheckpoint: String? = null, // Optional trusted checkpoint
    var includeCode: Boolean = false, // Default value
    var useAccesslist: Boolean = false,
    var zkProof: Boolean = false,
    var privacyMode: PrivacyMode = PrivacyMode.NONE, // PAP mode; BASIC sets verify flag
    var proverMode: ProverMode? = null, // null = auto-detect (REMOTE if provers configured, else LOCAL)
    var checkpointWitnessKeys: String? = null,
    var requestHandler: RequestHandler? = null // Add optional request handler for mocking
) {
    companion object {
        init {
//            println("Colibri: Initializing ...")
            // This will trigger the native library loading
            NativeLoader.loadLibrary()
            // Initialize the JNI bridge for storage callbacks
            // This call must happen after NativeLoader ensures the library is loaded.
            try {
                com.corpuscore.colibri.c4.nativeInitializeBridge()
       //         println("JNI Storage Bridge Initialized.")
            } catch (e: UnsatisfiedLinkError) {
                println("Error initializing JNI Storage Bridge: ${e.message}. Check native library loading and JNI function name.")
                // Depending on requirements, you might re-throw or handle this failure.
            }
        }

        // Static method to register the storage implementation
        fun registerStorage(storage: ColibriStorage) {
            StorageBridge.implementation = storage
//            println("ColibriStorage implementation registered.")
            // Optionally, trigger C-side re-configuration if needed, but likely handled at init.
        }
    }
    private val client = HttpClient(CIO) {
        engine {
             requestTimeout = 30_000 // 30 seconds timeout
        }
    }

    private val lightClientScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var lightClientJob: Job? = null

    /** Returns verify flags (e.g. VERIFY_FLAG_PAP, VERIFY_FLAG_OBLIVIOUS). PAP is enabled when BASIC or obliviousNodes is set. */
    private fun getVerifyFlags(): Long {
        val pap = privacyMode == PrivacyMode.BASIC || obliviousNodes.isNotEmpty()
        return (if (pap) 2L else 0L) or (if (obliviousNodes.isNotEmpty()) (1L shl 6) else 0L)
    }

    private fun serversForRequest(request: JSONObject, useProverFallback: Boolean = false): Array<String> {
        val type = request.optString("type", "eth_rpc")
        return when (type) {
            "checkpointz" -> checkpointz
            "beacon_api" -> if (useProverFallback && provers.isNotEmpty()) provers else beaconApis
            "prover" -> provers
            else -> {
                if (request.optJSONObject("payload")?.optString("method") == "eth_getProof" && obliviousNodes.isNotEmpty())
                    obliviousNodes
                else
                    ethRpcs
            }
        }
    }

    // Example method to demonstrate usage
    fun printConfig() {
        println("Chain ID: $chainId")
        println("ETH RPCs: ${ethRpcs.joinToString(", ")}")
        println("Beacon APIs: ${beaconApis.joinToString(", ")}")
        println("Trusted Checkpoint: ${trustedCheckpoint ?: "none"}")
        println("Include Code: $includeCode")
    }

    /**
     * Check what type of support a method has.
     *
     * In PAP mode the result may depend on cached data for the given params
     * (e.g. `eth_call` can become LOCAL when storage values are cached).
     *
     * @param method RPC method name
     * @param params Optional method parameters as JSON array string
     */
    suspend fun getMethodSupport(method: String, params: String? = null): MethodType {
         return withContext(Dispatchers.IO) {
             val typeInt = com.corpuscore.colibri.c4.c4_get_method_support(chainId, method, params ?: "", getVerifyFlags())
             MethodType.fromInt(typeInt)
         }
    }

    private suspend fun fetchRequest(servers: Array<String>, request: JSONObject) {

        // Define reqPtr before the loop so it's accessible in the final error case
        // Get req_ptr - now clean numeric JSON value after bprintf fix
        val reqPtr = request.getLong("req_ptr")
//        println("fetchRequest:  for req_ptr $reqPtr)")

        var index = 0
        var lastError = ""
        for (server in servers) {
            // Ensure all necessary fields are present or handle missing keys gracefully
            val exclude_mask = request.optInt("exclude_mask", 0)
            val uri = request.optString("url", "")
            val payload = request.optJSONObject("payload") // Use optJSONObject to handle missing payload gracefully
            val url = if (uri.isNotEmpty()) server.removeSuffix("/") + "/" + uri.removePrefix("/") else server
            val method = request.optString("method", "POST") // Default to POST if missing

            if (exclude_mask and (1 shl index) != 0) {
                index++
                continue
            }

            // --- Mocking Hook --- 
            if (requestHandler != null) {
                // Create a map representation of the request for the handler
                val requestDetails = mutableMapOf<String, Any?>()
                request.keys().forEach { key -> requestDetails[key] = request.get(key) }
                // Convert JSONObject/JSONArray within the details to maps/lists if necessary for easier handling?
                // For now, pass raw org.json objects within the map.

                val mockResponse = requestHandler!!(requestDetails)
                if (mockResponse != null) {
//                    println("fetchRequest: Mock response provided for req_ptr $reqPtr (size: ${mockResponse.size})")
                    com.corpuscore.colibri.c4.c4_req_set_response(reqPtr, mockResponse, index)
                    return // Skip actual network request
                }
            }
            // --- End Mocking Hook ---

            try {
                val response: HttpResponse = client.request(url) {
                    this.method = HttpMethod.parse(method)
                    if (request.getString("encoding") == "json") {
                        accept(ContentType.Application.Json)
                    } else {
                        accept(ContentType.Application.OctetStream)
                    }

                    if (payload != null && !payload.isEmpty()) {
                        contentType(ContentType.Application.Json)
                        setBody(payload.toString())
                    }
                }

                if (response.status.isSuccess()) {
                    // Success response handling with fixed req_ptr format
                    val responseBytes = response.readBytes()
                    com.corpuscore.colibri.c4.c4_req_set_response(reqPtr, responseBytes, index)
                    return
                }
                else {
                    lastError = response.status.toString()
                }
            } catch (e: Exception) {
                lastError = e.message ?: "Unknown error"
            }
            index++
        }
        // Error handling - now fixed for req_ptr parsing
        try {
            if (reqPtr != 0L && lastError.isNotEmpty()) {
                com.corpuscore.colibri.c4.c4_req_set_error(reqPtr, lastError, 0)
            }
        } catch (e: Exception) {
            println("fetchRequest: Error in c4_req_set_error: ${e.message}")
        }
    }

    private fun formatArg(arg: Any?): String = when (arg) { // Make arg nullable
        is BigInteger -> "\"0x${arg.toString(16)}\""  // Convert BigInteger to hex
        is Number -> "\"0x${arg.toLong().toString(16)}\""  // Convert numbers to hex
        is String -> {
             // Handle potential hex strings correctly within JSON
             if (arg.startsWith("0x") && arg.length > 2 && arg.substring(2).all { it.isDigit() || ('a'..'f').contains(it.lowercaseChar()) || ('A'..'F').contains(it.lowercaseChar()) }) {
                 "\"$arg\"" // Keep valid hex strings quoted
             } else {
                 // Escape backslashes and quotes for general strings
                 "\"${arg.replace("\\", "\\\\").replace("\"", "\\\"")}\""
             }
        }
        is Array<*> -> "[${arg.joinToString(",") { formatArg(it) }}]"  // Handle nested arrays (pass nulls as "null")
        is List<*> -> "[${arg.joinToString(",") { formatArg(it) }}]" // Also handle Lists
        null -> "null" // Represent null explicitly in JSON
        // Handle Map type by converting to JSON object string
        is Map<*, *> -> {
            val entries = arg.entries.joinToString(",") { (key, value) ->
                // Keys must be quoted strings in JSON
                val formattedKey = formatArg(key?.toString()) // Format key as JSON string
                val formattedValue = formatArg(value) // Format value recursively
                "$formattedKey: $formattedValue"
            }
            "{$entries}"
        }
        else -> "\"${arg.toString().replace("\\", "\\\\").replace("\"", "\\\"")}\"" // Quote and escape others
    }

    // Helper to format args array into JSON string
     private fun formatArgsArray(args: Array<Any?>): String {
         return "[${args.joinToString(",") { formatArg(it) }}]"
     }

    suspend fun getProof(method: String, args: Array<Any?>): ByteArray { // Allow nullable args
        return withContext(Dispatchers.IO) {
            val jsonArgs = formatArgsArray(args) // Use helper
            // Create the prover context with properly formatted JSON args
            val proverFlags = (if (includeCode) 1L else 0L) or (if (useAccesslist) (1L shl 6) else 0L)
            val ctx = com.corpuscore.colibri.c4.c4_create_prover_ctx(method, jsonArgs, chainId, proverFlags)
                ?: throw ColibriException("Failed to create prover context for method $method")

            // Add iteration limit to prevent infinite loops
            val maxIterations = 50
            var iteration = 0

            try {
                while (iteration < maxIterations) {
                    iteration++
//                    println("getProof: Iteration $iteration/$maxIterations")

                    // Execute the prover and get the JSON status
                     val statusJsonPtr = com.corpuscore.colibri.c4.c4_prover_execute_json_status(ctx)
                     if (statusJsonPtr == null) {
                        throw ColibriException("Prover execution returned null status for method $method")
                     }
                     val stateString = statusJsonPtr.toString() // Convert SWIG C pointer/object to string if needed
                     // TODO: Verify how SWIG handles string return. Assuming it's direct or needs .toString()

                    val state = try {
                         JSONObject(stateString)
                     } catch (e: Exception) {
                         throw ColibriException("Failed to parse prover status JSON: ${e.message}. JSON: $stateString")
                     }

                    when (state.getString("status")) {
                        "success" -> {
                             // Assuming c4_prover_get_proof returns ByteArray directly or a SWIG type convertible to it
                             val proofData = com.corpuscore.colibri.c4.c4_prover_get_proof(ctx)
                             // SWIG might return SWIGTYPE_p_unsigned_char or similar, needs explicit cast/conversion if not automatic
                             if (proofData is ByteArray) {
                                 return@withContext proofData
                             } else {
                                 // Handle unexpected type if necessary, this depends heavily on SWIG config
                                 throw ColibriException("Unexpected type returned by c4_prover_get_proof: ${proofData?.javaClass?.name}")
                             }
                        }
                        "error" -> {
                            throw ColibriException("Prover error for method $method: ${state.optString("error", "Unknown error")}")
                        }
                        "pending" -> {
//                            println("pending")
                            // Handle pending requests
                             val requests = state.optJSONArray("requests") ?: JSONArray() // Handle missing requests array
                            for (i in 0 until requests.length()) {
                                val request = requests.getJSONObject(i)
                                 // Ensure type field exists before accessing
                                 try {
                                     fetchRequest(serversForRequest(request), request)
                                 } catch (e: Exception) {
                                     // Log error during fetchRequest and potentially set error on C side if possible/needed
                                     println("Error handling pending request $i: ${e.message}")
                                     // Optionally rethrow or mark request as failed via c4_req_set_error if fetchRequest doesn't
                                 }
                            }
                        }
                         else -> throw ColibriException("Unknown prover status: ${state.getString("status")}")
                    }
                }

                // If loop finishes without success or error
                throw ColibriException("getProof exceeded max iterations ($maxIterations) for method $method without reaching success or error state.")

            } finally {
//                println("getProof: Freeing prover context")
                com.corpuscore.colibri.c4.c4_free_prover_ctx(ctx)
            }
        }
    }

    // Add fetchRpc helper function
    private suspend fun fetchRpc(urls: Array<String>, method: String, paramsJson: String, asProof: Boolean): ByteArray {
        var lastError: Exception = ColibriException("All RPC nodes failed for method $method")

        // Construct JSON RPC payload once
         val jsonRpcPayload = JSONObject()
         jsonRpcPayload.put("id", 1)
         jsonRpcPayload.put("jsonrpc", "2.0")
         jsonRpcPayload.put("method", method)
         // Parse the paramsJson string into an actual JSON array/object for the payload
         try {
             // Assume paramsJson is a valid JSON array like "[...]"
             jsonRpcPayload.put("params", JSONArray(paramsJson))
         } catch (e: Exception) {
             // Fallback or throw if paramsJson isn't a valid JSON array string
             // For simplicity, let's assume it's correct or handle specific cases if needed
             // Alternatively, could try parsing as JSONObject if that's possible for params
             throw ColibriException("Invalid params JSON provided to fetchRpc: $paramsJson")
         }
         if (asProof) {
             jsonRpcPayload.put("version", com.corpuscore.colibri.c4.c4_get_current_version_number())
         }
         val requestBody = jsonRpcPayload.toString()


        for (url in urls) {
            try {
//                println("fetchRpc: Sending $method to $url (Accept: ${if (asProof) "application/octet-stream" else "application/json"})")
                val response: HttpResponse = client.post(url) {
                    contentType(ContentType.Application.Json)
                    setBody(requestBody)
                    accept(if (asProof) ContentType.Application.OctetStream else ContentType.Application.Json)
                }

//                println("fetchRpc: Response from $url: Status ${response.status}")

                if (response.status.isSuccess()) {
                    val responseBytes = response.readBytes()
                     // Optional: Check Content-Type if !asProof before returning?
                     if (!asProof) {
                         val contentType = response.headers[HttpHeaders.ContentType]
                         if (contentType?.contains("application/json", ignoreCase = true) == true) {
                             // It's JSON as expected
                         } else if (responseBytes.isEmpty() && response.status == HttpStatusCode.NoContent) {
                            // Handle 204 No Content
                         }
                         else {
                             // Log warning if content type isn't JSON for non-proof requests
                             println("Warning: fetchRpc received non-JSON content type '$contentType' from $url for non-proof request.")
                         }
                     }
                    return responseBytes
                } else {
                     val errorBody = try { response.bodyAsText() } catch (e: Exception) { "Failed to read error body" }
                     // Try parsing JSON RPC error from body
                     var errorMessage = "HTTP Error ${response.status.value}"
                     if (response.headers[HttpHeaders.ContentType]?.contains("application/json", ignoreCase = true) == true) {
                          try {
                              val errorJson = JSONObject(errorBody)
                              if (errorJson.has("error") && errorJson.get("error") is JSONObject) {
                                   errorMessage = errorJson.getJSONObject("error").optString("message", errorMessage)
                              }
                          } catch (e: Exception) { /* Ignore parsing error, use HTTP status */ }
                     } else {
                         errorMessage += ": $errorBody"
                     }
                    lastError = ColibriException(errorMessage)
                    println("fetchRpc: Error from $url: $lastError")
                }
            } catch (e: Exception) {
                lastError = ColibriException("Network/Request error contacting $url: ${e.message}")
                 println("fetchRpc: Error contacting $url: $lastError")
            }
        }
        throw lastError // Throw the last encountered error if all URLs fail
    }

    // Adjust verifyProof return type to Any?
    suspend fun verifyProof(proof: ByteArray, method: String, args: Array<Any?>): Any? { // Allow nullable args, return Any?
        return withContext(Dispatchers.IO) {
            val jsonArgs = formatArgsArray(args) // Use helper
            val trustedCheckpointStr = trustedCheckpoint ?: ""

            val ctx = com.corpuscore.colibri.c4.c4_verify_create_ctx(proof, method, jsonArgs, chainId, trustedCheckpointStr, getVerifyFlags())
                 ?: throw ColibriException("Failed to create verifier context for method $method")

            // Add iteration limit to prevent infinite loops
            val maxIterations = 50
            var iteration = 0

            try {
                while (iteration < maxIterations) {
                    iteration++
//                    println("verifyProof: Iteration $iteration/$maxIterations")

                    // Execute the verifier and get the JSON status
                    // Again, assuming SWIG handles char* return correctly. **VERIFY THIS**.
                    val statusJsonPtr = com.corpuscore.colibri.c4.c4_verify_execute_json_status(ctx)
                    if (statusJsonPtr == null) {
                        throw ColibriException("Verifier execution returned null status for method $method")
                    }
                     val stateString = statusJsonPtr.toString() // Convert SWIG C pointer/object to string
                     // TODO: Verify how SWIG handles string return.

                    val state = try {
                         JSONObject(stateString)
                     } catch (e: Exception) {
                          throw ColibriException("Failed to parse verifier status JSON: ${e.message}. JSON: $stateString")
                     }

                    when (state.getString("status")) {
                        "success" -> {
                            // Extract the 'result' field. It could be any JSON type.
                             if (state.has("result")) {
                                 // org.json returns basic types (String, Int, Boolean, Long, Double),
                                 // JSONObject, JSONArray, or JSONObject.NULL
                                 val result = state.get("result")
                                 if (result == JSONObject.NULL) {
                                      return@withContext null
                                 }
                                 // Convert JSONArray to List<Any?> and JSONObject to Map<String, Any?> for more Kotlin-idiomatic return
                                 return@withContext convertJsonToJava(result) // Use the helper
                             } else {
                                // Success status but no result field - interpret as null or void success?
                                return@withContext null
                             }
                        }
                        "revert" -> {
                            // EVM ran to completion but reverted -- a fully verified
                            // outcome. Expose the raw revert data so callers can decode
                            // OffchainLookup (EIP-3668) / custom Solidity errors.
                            throw ColibriRevertException(state.optString("data", "0x"))
                        }
                        "error" -> {
                             throw ColibriException("Verifier error for method $method: ${state.optString("error", "Unknown error")}")
                        }
                        "pending" -> {
                            // Handle pending requests
                            val requests = state.optJSONArray("requests") ?: JSONArray() // Handle missing requests array
                            for (i in 0 until requests.length()) {
                                val request = requests.getJSONObject(i)
                                 try {
                                     fetchRequest(serversForRequest(request, useProverFallback = true), request)
                                 } catch (e: Exception) {
                                     println("Error handling pending request $i: ${e.message}")
                                     // Consider setting error on C side via c4_req_set_error
                                 }
                            }
                        }
                         else -> throw ColibriException("Unknown verifier status: ${state.getString("status")}")
                    }
                }

                // If loop finishes without success or error
                throw ColibriException("verifyProof exceeded max iterations ($maxIterations) for method $method without reaching success or error state.")

            } finally {
//                println("verifyProof: Freeing verifier context")
                com.corpuscore.colibri.c4.c4_verify_free_ctx(ctx)
            }
        }
    }

    // Unified RPC execution via the C core state machine.
    suspend fun rpc(method: String, args: Array<Any?>): Any? {
        return withContext(Dispatchers.IO) {
            val jsonArgs = formatArgsArray(args)
            val proverFlags = (if (includeCode) 1L else 0L) or (if (useAccesslist) (1L shl 6) else 0L) or (if (zkProof) (1L shl 7) else 0L)
            val resolvedMode = proverMode ?: if (provers.isEmpty()) ProverMode.LOCAL else ProverMode.REMOTE
            val nativeMode = if (resolvedMode == ProverMode.LIGHT_CLIENT) ProverMode.HYBRID.value else resolvedMode.value

            val ctx = com.corpuscore.colibri.c4.c4_create_rpc_ctx(method, jsonArgs, chainId, proverFlags, getVerifyFlags(), nativeMode)
                ?: throw ColibriException("Failed to create RPC context for method $method")

            if (resolvedMode == ProverMode.PROXY) {
                com.corpuscore.colibri.c4.c4_rpc_set_proxy_urls(ctx, ethRpcs.joinToString(","), beaconApis.joinToString(","))
            }

            val checkpointStr = trustedCheckpoint
            if (!checkpointStr.isNullOrEmpty()) {
                com.corpuscore.colibri.c4.c4_set_checkpoint(chainId, checkpointStr)
            }
            val witnessKeys = checkpointWitnessKeys
            if (!witnessKeys.isNullOrEmpty()) {
                com.corpuscore.colibri.c4.c4_rpc_set_witness_keys(ctx, witnessKeys)
            }

            val maxIterations = 50
            var iteration = 0

            try {
                while (iteration < maxIterations) {
                    iteration++
                    val statusJsonPtr = com.corpuscore.colibri.c4.c4_rpc_execute_json_status(ctx)
                        ?: throw ColibriException("RPC execution returned null status for method $method")
                    val stateString = statusJsonPtr.toString()

                    val state = try {
                        JSONObject(stateString)
                    } catch (e: Exception) {
                        throw ColibriException("Failed to parse RPC status JSON: ${e.message}")
                    }

                    when (state.getString("status")) {
                        "success" -> {
                            if (state.has("result")) {
                                val result = state.get("result")
                                return@withContext if (result == JSONObject.NULL) null else convertJsonToJava(result)
                            }
                            return@withContext null
                        }
                        "revert" -> {
                            // EVM ran to completion but reverted -- a fully verified
                            // outcome. Expose the raw revert data so callers can decode
                            // OffchainLookup (EIP-3668) / custom Solidity errors.
                            throw ColibriRevertException(state.optString("data", "0x"))
                        }
                        "error" -> {
                            throw ColibriException("RPC error for method $method: ${state.optString("error", "Unknown error")}")
                        }
                        "pending" -> {
                            val requests = state.optJSONArray("requests") ?: JSONArray()
                            for (i in 0 until requests.length()) {
                                val request = requests.getJSONObject(i)
                                try {
                                    fetchRequest(serversForRequest(request, useProverFallback = true), request)
                                } catch (e: Exception) {
                                    println("Error handling pending request $i: ${e.message}")
                                }
                            }
                        }
                        else -> throw ColibriException("Unknown RPC status: ${state.getString("status")}")
                    }
                }
                throw ColibriException("rpc exceeded max iterations ($maxIterations) for method $method")
            } finally {
                com.corpuscore.colibri.c4.c4_free_rpc_ctx(ctx)
            }
        }
    }

    /**
     * Starts background polling to keep the block header cache warm.
     * Useful for [ProverMode.LIGHT_CLIENT].
     *
     * By default polls `eth_getBlockHeader("latest")` which fetches only the
     * compact header proof. Set [fullBlock] to `true` to poll
     * `eth_getBlockByNumber("latest")` instead -- useful when many
     * `eth_getTransactionByHash` / `eth_getTransactionReceipt` calls follow,
     * since those need the full block data.
     *
     * @param intervalMs polling interval in milliseconds (default: 12000 = one Ethereum slot)
     * @param fullBlock if true, fetch the full block instead of just the header (default: false)
     */
    fun startLightClient(intervalMs: Long = 12_000, fullBlock: Boolean = false) {
        stopLightClient()
        val method = if (fullBlock) "eth_getBlockByNumber" else "eth_getBlockHeader"
        val params: Array<Any?> = if (fullBlock) arrayOf("latest", false) else arrayOf("latest")
        lightClientJob = lightClientScope.launch {
            while (true) {
                try {
                    rpc(method, params)
                } catch (_: Exception) { }
                delay(intervalMs)
            }
        }
    }

    /** Stops background light client polling started by [startLightClient]. */
    fun stopLightClient() {
        lightClientJob?.cancel()
        lightClientJob = null
    }
}

// Helper function to convert org.json types to standard Java/Kotlin types
internal fun convertJsonToJava(jsonValue: Any?): Any? {
    return when (jsonValue) {
        is JSONObject -> {
            val map = mutableMapOf<String, Any?>()
            jsonValue.keys().forEach { key ->
                map[key] = convertJsonToJava(jsonValue.get(key))
            }
            map
        }
        is JSONArray -> {
            val list = mutableListOf<Any?>()
            for (i in 0 until jsonValue.length()) {
                list.add(convertJsonToJava(jsonValue.get(i)))
            }
            list
        }
        JSONObject.NULL -> null
        // Basic types are returned as-is by org.json
        else -> jsonValue
    }
}