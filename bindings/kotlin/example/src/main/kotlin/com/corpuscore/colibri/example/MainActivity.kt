package com.corpuscore.colibri.example

import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.corpuscore.colibri.Colibri
import com.corpuscore.colibri.ColibriException
import kotlinx.coroutines.launch
import java.math.BigInteger

class MainActivity : AppCompatActivity() {
    
    private lateinit var blockNumberText: TextView
    private lateinit var statusText: TextView
    private lateinit var refreshButton: Button
    private lateinit var colibri: Colibri

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // Initialize views
        blockNumberText = findViewById(R.id.blockNumberText)
        statusText = findViewById(R.id.statusText)
        refreshButton = findViewById(R.id.refreshButton)

        // Initialize Colibri with Ethereum Mainnet configuration
        try {
            colibri = Colibri(
                chainId = BigInteger.ONE, // Ethereum Mainnet
                ethRpcs = arrayOf(
                    "https://rpc.ankr.com/eth",
                    "https://eth.public-rpc.com",
                    "https://ethereum.publicnode.com"
                ),
                beaconApis = arrayOf(
                    "https://lodestar-mainnet.chainsafe.io",
                    "https://beaconstate.info",
                    "https://mainnet.beacon.publicnode.com"
                ),
                proofers = arrayOf("https://c4.incubed.net")
            )
            
            // Set up refresh button click listener
            refreshButton.setOnClickListener {
                fetchBlockNumber()
            }
            
            // Fetch block number on startup
            fetchBlockNumber()
            
        } catch (e: Exception) {
            statusText.text = getString(R.string.initialization_failed)
            blockNumberText.text = getString(R.string.error_prefix) + e.message
            refreshButton.isEnabled = false
        }
    }

    private fun fetchBlockNumber() {
        lifecycleScope.launch {
            try {
                // Disable button during request
                refreshButton.isEnabled = false
                statusText.text = getString(R.string.fetching_block_number)
                blockNumberText.text = getString(R.string.loading)

                // Call eth_blockNumber via Colibri RPC
                val result = colibri.rpc("eth_blockNumber", arrayOf())
                
                // Parse the result
                val blockNumberHex = result?.toString() ?: "0x0"
                val blockNumber = if (blockNumberHex.startsWith("0x")) {
                    BigInteger(blockNumberHex.substring(2), 16)
                } else {
                    BigInteger(blockNumberHex)
                }

                // Update UI
                blockNumberText.text = "#${blockNumber.toString()}"
                statusText.text = "Last updated: ${System.currentTimeMillis() / 1000}"

            } catch (e: ColibriException) {
                blockNumberText.text = getString(R.string.error_prefix) + e.message
                statusText.text = "Colibri error occurred"
            } catch (e: Exception) {
                blockNumberText.text = getString(R.string.error_prefix) + "Network error"
                statusText.text = "Exception: ${e.message}"
            } finally {
                // Re-enable button
                refreshButton.isEnabled = true
            }
        }
    }
}