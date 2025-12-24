use colibri::{ProverClient, Verifier, Result};

#[tokio::main]
async fn main() -> Result<()> {
    println!("=== Colibri Proof Generation and Verification Example ===\n");

    println!("Step 1: Generating proof...");
    println!("{}", "-".repeat(40));

    let method = "eth_blockNumber";
    let params = r#"[]"#;
    let chain_id = 1;
    let flags = 0;

    let beacon_api_url = std::env::var("BEACON_API_URL")
        .unwrap_or_else(|_| "https://lodestar-mainnet.chainsafe.io".to_string());

    let eth_rpc_url = std::env::var("ETH_RPC_URL")
        .unwrap_or_else(|_| "https://ethereum-rpc.publicnode.com".to_string());

    println!("Method: {}", method);
    println!("Params: {}", params);
    println!("Chain ID: {}", chain_id);

    let mut client = ProverClient::with_urls(
        method,
        params,
        chain_id,
        flags,
        Some(beacon_api_url),
        Some(eth_rpc_url),
    )?;

    let proof = match client.run_to_completion().await {
        Ok(proof) => {
            println!("✅ Proof generated successfully!");
            println!("   Size: {} bytes", proof.len());
            proof
        }
        Err(e) => {
            println!("❌ Failed to generate proof: {}", e);
            return Err(e);
        }
    };

    println!("\nStep 2: Verifying proof...");
    println!("{}", "-".repeat(40));

    let mut verifier = Verifier::new(
        &proof,
        method,
        params,
        chain_id,
        "",
    )?;
    println!("✅ Verifier created");

    let client = reqwest::Client::new();
    let beacon_api_base = "https://lodestar-mainnet.chainsafe.io";

    loop {
        let status_json = verifier.execute_json_status()?;
        let status: serde_json::Value = serde_json::from_str(&status_json)?;

        match status["status"].as_str() {
            Some("success") => {
                println!("✅ Proof verification SUCCESSFUL!");

                // Get the verified result
                if let Some(result) = status.get("result") {
                    println!("\nVerified Result:");
                    println!("{}", serde_json::to_string_pretty(result)?);
                }
                break;
            }
            Some("error") => {
                let msg = status["error"].as_str().unwrap_or("Unknown error");
                println!("❌ Verification failed: {}", msg);
                return Err(colibri::ColibriError::Ffi(msg.to_string()));
            }
            Some("pending") => {
                if let Some(requests) = status["requests"].as_array() {
                    for request in requests {
                        let url = request["url"].as_str().unwrap_or("");
                        let method = request["method"].as_str().unwrap_or("GET");
                        let req_ptr = request["req_ptr"].as_u64().unwrap_or(0);
                        let request_type = request["type"].as_str().unwrap_or("");
                        let encoding = request["encoding"].as_str().unwrap_or("json");

                        let full_url = if request_type == "beacon_api" || request_type == "checkpointz" {
                            format!("{}/{}", beacon_api_base, url)
                        } else if url.starts_with("http://") || url.starts_with("https://") {
                            url.to_string()
                        } else {
                            url.to_string()
                        };
                        let mut request_builder = if method == "POST" || method == "post" {
                            let payload = request["payload"].as_str().unwrap_or("{}");
                            client.post(&full_url)
                                .header("Content-Type", "application/json")
                                .body(payload.to_string())
                        } else {
                            client.get(&full_url)
                        };

                        if encoding == "ssz" {
                            request_builder = request_builder.header("Accept", "application/octet-stream");
                        } else {
                            request_builder = request_builder.header("Accept", "application/json");
                        }

                        let response = request_builder.send().await?;
                        let response_bytes = response.bytes().await?;
                        colibri::helpers::set_request_response(req_ptr, &response_bytes, 0);
                    }
                }
            }
            _ => {
                println!("Unknown status: {}", status_json);
                break;
            }
        }
    }

    println!("\n🎉 Complete! The proof has been generated and verified.");

    Ok(())
}