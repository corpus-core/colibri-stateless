use colibri::{ProverClient, Result};

#[tokio::main]
async fn main() -> Result<()> {
    println!("=== Colibri Proof Generation Example ===\n");

    // Configure the RPC method and parameters
    let method = "eth_getBalance";
    let params = r#"["0x742d35Cc6634C0532925a3b844Bc9e7595f0bEb0", "latest"]"#;
    let chain_id = 1; // Ethereum mainnet
    let flags = 0;

    // You need to provide your own node URLs or use public endpoints
    let beacon_api_url = std::env::var("BEACON_API_URL")
        .unwrap_or_else(|_| "https://lodestar-mainnet.chainsafe.io".to_string());

    let eth_rpc_url = std::env::var("ETH_RPC_URL")
        .unwrap_or_else(|_| "https://eth-mainnet.public.blastapi.io".to_string());

    println!("Configuration:");
    println!("  Method: {}", method);
    println!("  Params: {}", params);
    println!("  Chain ID: {}", chain_id);
    println!("  Beacon API: {}", beacon_api_url);
    println!("  Ethereum RPC: {}", eth_rpc_url);
    println!();

    // Create the prover client with URLs
    let mut client = ProverClient::with_urls(
        method,
        params,
        chain_id,
        flags,
        Some(beacon_api_url),
        Some(eth_rpc_url),
    )?;

    println!("Starting proof generation...");
    println!("This will make multiple HTTP requests to generate a cryptographic proof.\n");

    // Run to completion - this handles all HTTP callbacks automatically
    match client.run_to_completion().await {
        Ok(proof) => {
            println!("\n✅ SUCCESS! Proof generated.");
            println!("=====================================");
            println!("Proof size: {} bytes", proof.len());

            // Show first 100 bytes as hex
            if proof.len() > 0 {
                println!("\nFirst 100 bytes of proof (hex):");
                let display_len = proof.len().min(100);
                for i in (0..display_len).step_by(16) {
                    print!("  {:04x}: ", i);
                    for j in i..std::cmp::min(i + 16, display_len) {
                        print!("{:02x} ", proof[j]);
                    }
                    println!();
                }

                if proof.len() > 100 {
                    println!("  ... ({} more bytes)", proof.len() - 100);
                }
            }

            // Try to parse as JSON if possible (some proofs are JSON)
            if let Ok(json_str) = std::str::from_utf8(&proof) {
                if let Ok(json) = serde_json::from_str::<serde_json::Value>(json_str) {
                    println!("\nProof appears to be JSON:");
                    println!("{}", serde_json::to_string_pretty(&json)?);
                }
            }

            println!("\n🎉 Full proof generation completed successfully!");
            println!("This proof can be verified by any Colibri verifier.");
        }
        Err(e) => {
            println!("\n❌ ERROR: Failed to generate proof");
            println!("=====================================");
            println!("Error: {}", e);
            println!("\nPossible causes:");
            println!("- Invalid RPC endpoint URL");
            println!("- RPC endpoint doesn't support required methods");
            println!("- Network connectivity issues");
            println!("- Beacon node sync issues");

            return Err(e);
        }
    }

    Ok(())
}