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
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject // Use org.json for JSON parsing
import org.json.JSONArray // Add JSONArray import
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

// Custom Exception for Colibri errors
class ColibriException(message: String) : RuntimeException(message)

// Interface for storage operations callback
interface ColibriStorage {
    fun get(key: String): ByteArray? // Return nullable ByteArray
    fun set(key: String, value: ByteArray)
    fun delete(key: String) // Changed 'del' to 'delete' for Kotlin convention
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
    var trustedBlockHashes: Array<String> = arrayOf(), // Default empty array
    var includeCode: Boolean = false, // Default value
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

    // Example method to demonstrate usage
    fun printConfig() {
        println("Chain ID: $chainId")
        println("ETH RPCs: ${ethRpcs.joinToString(", ")}")
        println("Beacon APIs: ${beaconApis.joinToString(", ")}")
        println("Trusted Block Hashes: ${trustedBlockHashes.joinToString(", ")}")
        println("Include Code: $includeCode")
    }

    // Add getMethodSupport function
    suspend fun getMethodSupport(method: String): MethodType {
         return withContext(Dispatchers.IO) {
             // Use the correct function name generated by SWIG
             val typeInt = com.corpuscore.colibri.c4.c4_get_method_support(chainId, method)
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
            val ctx = com.corpuscore.colibri.c4.c4_create_prover_ctx(method, jsonArgs, chainId, if (includeCode) 1 else 0)
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
                                 val type = request.optString("type", "eth_rpc") // Default or handle missing type
                                val servers = when (type) {
                                    "checkpointz" -> checkpointz
                                    "beacon_api" -> beaconApis
                                    else -> ethRpcs
                                }
                                 try {
                                     fetchRequest(servers, request)
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
             val trustedHashesJson = formatArgsArray(trustedBlockHashes.map { it as Any? }.toTypedArray()) // Format trusted hashes

            // Assuming c4_verify_create_ctx takes JSON strings for args and trusted hashes
            val ctx = com.corpuscore.colibri.c4.c4_verify_create_ctx(proof, method, jsonArgs, chainId, trustedHashesJson)
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
                        "error" -> {
                             throw ColibriException("Verifier error for method $method: ${state.optString("error", "Unknown error")}")
                        }
                        "pending" -> {
                            // Handle pending requests
                            val requests = state.optJSONArray("requests") ?: JSONArray() // Handle missing requests array
                            for (i in 0 until requests.length()) {
                                val request = requests.getJSONObject(i)
                                 val type = request.optString("type", "eth_rpc")
                                // Prioritize provers if not empty and type is beacon_api
                                val servers = when (type) {
                                    "checkpointz" -> checkpointz
                                    "beacon_api" -> if (provers.isNotEmpty()) provers else beaconApis
                                    else -> ethRpcs
                                }
                                 try {
                                     fetchRequest(servers, request)
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

     // Add rpc method implementation
     suspend fun rpc(method: String, args: Array<Any?>): Any? { // Allow nullable args, return Any?
         val methodType = getMethodSupport(method)
         var proof: ByteArray = byteArrayOf() // Initialize empty proof

//         println("rpc: Method $method, Type: $methodType, Args: ${formatArgsArray(args)}")

         when (methodType) {
             MethodType.PROOFABLE -> {
                 // TODO: Implement optional verify hook if needed
                 if (provers.isNotEmpty()) {
                  //   println("rpc: Fetching proof for $method from prover...")
                     proof = try {
                          fetchRpc(provers, method, formatArgsArray(args), true)
                     } catch (e: Exception) {
                          println("rpc: Failed to fetch proof from prover, falling back to local creation. Error: ${e.message}")
                          println("rpc: Creating proof locally for $method...")
                          getProof(method, args) // Create proof locally if fetch fails
                     }

                 } else {
//                     println("rpc: Creating proof locally for $method...")
                     proof = getProof(method, args)
                 }
//                 println("rpc: Obtained proof (${proof.size} bytes) for $method.")
                 // Verification happens below
             }
             MethodType.UNPROOFABLE -> {
//                 println("rpc: Method $method is UNPROOFABLE, fetching directly...")
                 val responseData = fetchRpc(ethRpcs, method, formatArgsArray(args), false)
//                 println("rpc: Fetched direct response (${responseData.size} bytes) for $method.")
                 // Parse JSON response
                 return try {
                      val jsonString = responseData.toString(Charsets.UTF_8)
                      if (jsonString.isBlank()) { // Handle empty response (e.g., 204 No Content)
                            null
                      } else {
                          val jsonResponse = JSONObject(jsonString)
                          if (jsonResponse.has("error") && jsonResponse.get("error") != JSONObject.NULL) {
                              val errorObj = jsonResponse.getJSONObject("error")
                              throw ColibriException("RPC Error for $method: ${errorObj.optString("message", "Unknown error")}")
                          }
                          if (jsonResponse.has("result")) {
                              val result = jsonResponse.get("result")
                              if (result == JSONObject.NULL) null else result // Return null if JSON result is null
                          } else {
                               // Neither error nor result - treat as success with null result?
                               null
                          }
                      }

                 } catch (e: Exception) {
                      throw ColibriException("Failed to parse direct RPC response for $method: ${e.message}")
                 }
             }
             MethodType.NOT_SUPPORTED -> {
                 println("rpc: Method $method is NOT_SUPPORTED.")
                 throw ColibriException("Method $method is not supported")
             }
             MethodType.LOCAL -> {
//                 println("rpc: Method $method is LOCAL, proceeding with verification (empty proof).")
                 proof = byteArrayOf() // Ensure proof is empty for local verification
                 // Verification happens below
             }
             MethodType.UNKNOWN -> {
                 println("rpc: Method $method has UNKNOWN type.")
                 throw ColibriException("Method $method has unknown support type")
             }
         }

         // Verify the proof (either created/fetched for PROOFABLE, or empty for LOCAL)
//         println("rpc: Verifying proof for $method...")
         return verifyProof(proof, method, args)
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