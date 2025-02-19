package com.corpuscore.colibri

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject // Use org.json for JSON parsing

class Colibri(private val chainId: Long, private val ethRpcs: Array<String>, private val beaconApis: Array<String>) {

    suspend fun getProof(method: String, args: Array<Any>): ByteArray {
        return withContext(Dispatchers.IO) {
            var error: String? = null
            var proof: ByteArray? = null

            // Create the proofer context
            val ctx = colibriJNI.create_proofer_ctx(method, args.joinToString(","), chainId)

            try {
                while (true) {
                    // Execute the proofer and get the JSON status
                    val stateString = colibriJNI.proofer_execute_json_status(ctx)
                    val state = JSONObject(stateString) // Parse JSON string to JSONObject

                    when (state.getString("status")) {
                        "success" -> {
                            val resultPtr = state.getLong("result")
                            val resultLen = state.getInt("result_len")
                            proof = ByteArray(resultLen)
                            // Access C-pointers in Kotlin
                            // Assuming you have a method to copy data from native memory to ByteArray
                            colibriJNI.copyFromNative(resultPtr, proof, resultLen)
                            break
                        }
                        "error" -> {
                            error = state.getString("error")
                            break
                        }
                        else -> {
                            // Handle pending requests
                            val requests = state.getJSONArray("requests")
                            for (i in 0 until requests.length()) {
                                val request = requests.getJSONObject(i)
                                val response = fetchRequest(request)
                                if (response.isNotEmpty()) {
                                    val target = colibriJNI.req_create_response(request.getLong("req_ptr"), response.size.toLong(), 0)
                                    // Copy response data to the target
                                    copyToNative(response, target)
                                } else {
                                    colibriJNI.req_set_error(request.getLong("req_ptr"), "Failed to fetch data", 0)
                                }
                            }
                        }
                    }
                }
            } finally {
                colibriJNI.free_proofer_ctx(ctx)
            }

            proof ?: throw RuntimeException(error ?: "Failed to generate proof")
        }
    }

    private suspend fun fetchRequest(request: JSONObject): ByteArray {
        // Implement the network request logic here
        // Use libraries like Ktor or OkHttp for HTTP requests
        return ByteArray(0) // Placeholder
    }

    private fun copyFromNative(srcPtr: Long, dest: ByteArray, length: Int) {
        // Implement native memory copy logic
    }

    private fun copyToNative(src: ByteArray, destPtr: Long) {
        // Implement native memory copy logic
    }
}