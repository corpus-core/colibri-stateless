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
    private var chainId: BigInteger = BigInteger.ONE, // Default value
    private var ethRpcs: Array<String> = arrayOf("https://default.rpc"), // Default value
    private var beaconApis: Array<String> = arrayOf("https://default.beacon") // Default value
) {
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

    // Example method to demonstrate usage
    fun printConfig() {
        println("Chain ID: $chainId")
        println("ETH RPCs: ${ethRpcs.joinToString(", ")}")
        println("Beacon APIs: ${beaconApis.joinToString(", ")}")
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

    suspend fun getProof(method: String, args: Array<Any>): ByteArray {
        return withContext(Dispatchers.IO) {
            var error: String? = null
            var proof: ByteArray? = null

            // Create the proofer context
            val ctx = com.corpuscore.colibri.c4.create_proofer_ctx(method, args.joinToString(","), chainId)

            try {
                while (true) {
                    // Execute the proofer and get the JSON status
                    val stateString = com.corpuscore.colibri.c4.proofer_execute_json_status(ctx)
                    val state = JSONObject(stateString) // Parse JSON string to JSONObject

                    when (state.getString("status")) {
                        "success" -> {
                            return@withContext com.corpuscore.colibri.c4.proofer_get_proof(ctx)
                        }
                        "error" -> {
                            throw RuntimeException(state.getString("error")) 
                        }
                        else -> {
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
            throw RuntimeException("No proof could be generated")
        }
    }
}