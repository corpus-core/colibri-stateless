package com.corpuscore.colibri

// Import the SWIG-generated class explicitly
//import c4
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject // Use org.json for JSON parsing
import java.math.BigInteger
import io.ktor.client.*
import io.ktor.client.engine.cio.*
import io.ktor.client.request.*
import io.ktor.client.statement.*
import io.ktor.http.*

class Colibri(
    var chainId: BigInteger = BigInteger.ONE, // Default value
    var ethRpcs: Array<String> = arrayOf("https://rpc.ankr.com/eth"), // Default value
    var beaconApis: Array<String> = arrayOf("https://lodestar-mainnet.chainsafe.io"), // Default value
    var trustedBlockHashes: Array<String> = arrayOf() // Default empty array
) {
    companion object {
        init {
            // This will trigger the native library loading
            NativeLoader
        }
    }
    private val client = HttpClient(CIO)

    // Example method to demonstrate usage
    fun printConfig() {
        println("Chain ID: $chainId")
        println("ETH RPCs: ${ethRpcs.joinToString(", ")}")
        println("Beacon APIs: ${beaconApis.joinToString(", ")}")
        println("Trusted Block Hashes: ${trustedBlockHashes.joinToString(", ")}")
    }

    private suspend fun fetchRequest(servers: Array<String>, request: JSONObject) {
        var index = 0
        var lastError = ""
        for (server in servers) {
            val exclude_mask = request.getInt("exclude_mask")
            val uri = request.getString("url")
            val payload = request.getJSONObject("payload")
            val url = if (uri.isNotEmpty()) server + "/" + uri else server
            val method = request.getString("method")

            if (exclude_mask and (1 shl index) != 0) {
                index++
                continue
            }

            try {
                val response: HttpResponse = client.request(url) {
                    this.method = HttpMethod.parse(method)
                    if (request.getString("encoding") == "json") {
                        accept(ContentType.Application.Json)
                    } else {
                        accept(ContentType.Application.OctetStream)
                    }

                    if (!payload.isEmpty()) {
                        contentType(ContentType.Application.Json)
                        setBody(payload.toString())
                    }
                }

                if (response.status.isSuccess()) {
                    c4.req_set_response(request.getLong("req_ptr"), response.readBytes(), index)
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

        c4.req_set_error(request.getLong("req_ptr"), lastError, 0)
    }

    private fun formatArg(arg: Any): String = when (arg) {
        is BigInteger -> "\"0x${arg.toString(16)}\""  // Convert BigInteger to hex
        is Number -> "\"0x${arg.toLong().toString(16)}\""  // Convert numbers to hex
        is String -> {
            if (arg.startsWith("0x")) "\"$arg\""  // Keep hex strings as-is
            else "\"$arg\""  // Quote regular strings
        }
        is Array<*> -> "[${arg.joinToString(",") { formatArg(it ?: "null") }}]"  // Handle nested arrays
        else -> "\"$arg\""  // Quote everything else
    }

    suspend fun getProof(method: String, args: Array<Any>): ByteArray {
        return withContext(Dispatchers.IO) {
            
            // Create the proofer context with properly formatted JSON args
            val ctx = com.corpuscore.colibri.c4.create_proofer_ctx(method, "[${args.joinToString(",") { formatArg(it) }}]", chainId)

            try {
                while (true) {
                    // Execute the proofer and get the JSON status
                    val state = JSONObject(com.corpuscore.colibri.c4.proofer_execute_json_status(ctx))

                    when (state.getString("status")) {
                        "success" -> {
                            return@withContext com.corpuscore.colibri.c4.proofer_get_proof(ctx) as ByteArray
                        }
                        "error" -> {
                            throw RuntimeException(state.getString("error")) 
                        }
                        "pending" -> {
                            // Handle pending requests
                            val requests = state.getJSONArray("requests")
                            for (i in 0 until requests.length()) {
                                val request = requests.getJSONObject(i)
                                val servers = if (request.getString("type") == "eth_rpc") ethRpcs else beaconApis
                                fetchRequest(servers, request)
                            }
                        }
                    }
                }
            } finally {
                com.corpuscore.colibri.c4.free_proofer_ctx(ctx)
            }
            
            // This line should never be reached due to the infinite loop and return/throw statements above
            throw RuntimeException("Unexpected end of getProof method")
        }
    }

    suspend fun verifyProof(proof: ByteArray, method: String, args: Array<Any>): JSONObject {
        return withContext(Dispatchers.IO) {
            val jsonArgs = "[${args.joinToString(",") { formatArg(it) }}]"
            val ctx = com.corpuscore.colibri.c4.verify_create_ctx(proof, method, jsonArgs, chainId, "[${trustedBlockHashes.joinToString(",") { formatArg(it) }}]")


            try {
                while (true) {
                    // Execute the proofer and get the JSON status
                    val stateString = com.corpuscore.colibri.c4.verify_execute_json_status(ctx)
                    val state = JSONObject(stateString)

                    when (state.getString("status")) {
                        "success" -> {
                            return@withContext state.getJSONObject("result")
                        }
                        "error" -> {
                            throw RuntimeException(state.getString("error")) 
                        }
                        "pending" -> {
                            // Handle pending requests
                            val requests = state.getJSONArray("requests")
                            for (i in 0 until requests.length()) {
                                val request = requests.getJSONObject(i)
                                val servers = if (request.getString("type") == "eth_rpc") ethRpcs else beaconApis
                                fetchRequest(servers, request)
                            }
                        }
                    }
                }
            } finally {
                com.corpuscore.colibri.c4.verify_free_ctx(ctx)
            }
            
            // This line should never be reached due to the infinite loop and return/throw statements above
            throw RuntimeException("Unexpected end of verify method")
        }
    }
}