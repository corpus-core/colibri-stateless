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
    var ethRpcs: Array<String> = arrayOf("https://default.rpc"), // Default value
    var beaconApis: Array<String> = arrayOf("https://default.beacon"), // Default value
    var trustedBlockHashes: Array<String> = arrayOf() // Default empty array
) {
    companion object {
        init {
            // This will trigger the native library loading
            NativeLoader
        }
    }
    private val client = HttpClient(CIO)

    // Setter for chainId
    fun setChainId(newChainId: BigInteger) {
        chainId = newChainId
    }

    // Setter for ethRpcs
    fun setEthRpcs(newEthRpcs: Array<String>) {
        ethRpcs = newEthRpcs
    }

    // Setter for beaconApis
    fun setBeaconApis(newBeaconApis: Array<String>) {
        beaconApis = newBeaconApis
    }

    // Setter for trustedBlockHashes
    fun setTrustedBlockHashes(newTrustedBlockHashes: Array<String>) {
        trustedBlockHashes = newTrustedBlockHashes
    }

    // Example method to demonstrate usage
    fun printConfig() {
        println("Chain ID: $chainId")
        println("ETH RPCs: ${ethRpcs.joinToString(", ")}")
        println("Beacon APIs: ${beaconApis.joinToString(", ")}")
        println("Trusted Block Hashes: ${trustedBlockHashes.joinToString(", ")}")
    }

    private suspend fun fetchRequest(servers: Array<String>, request: JSONObject) {
        var index = 0
        for (server in servers) {
            val payload = request.getJSONObject("payload")
            val url = server + "/" + request.getString("url")
            val method = request.getString("method")

            try {
                val response: HttpResponse = client.request(url) {
                    this.method = HttpMethod.parse(method)
                    contentType(ContentType.Application.Json)
                    setBody(payload.toString())
                }

                if (response.status.isSuccess()) {
                    c4.req_set_response(request.getLong("req_ptr"), response.readBytes(), index)
                    return
                }
            } catch (e: Exception) {
                // Handle exceptions, e.g., log them or retry
            }
            index++
        }
        throw RuntimeException("No response from any server")
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
            // Format arguments as JSON array
            val jsonArgs = "[${args.joinToString(",") { formatArg(it) }}]"
            
            // Create the proofer context with properly formatted JSON args
            val ctx = com.corpuscore.colibri.c4.create_proofer_ctx(method, jsonArgs, chainId)

            try {
                while (true) {
                    // Execute the proofer and get the JSON status
                    val stateString = com.corpuscore.colibri.c4.proofer_execute_json_status(ctx)
                    val state = JSONObject(stateString)

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