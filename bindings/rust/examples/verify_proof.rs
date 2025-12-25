use colibri::{Verifier, Result};
use std::fs;
use std::env;

#[tokio::main]
async fn main() -> Result<()> {
    // Get proof file from command line argument or use default
    let args: Vec<String> = env::args().collect();
    let proof_file = if args.len() > 1 {
        &args[1]
    } else {
        "rust_proof.bin"
    };

    let proof = fs::read(proof_file)
        .expect(&format!("Failed to read {}", proof_file));

    println!("Loaded proof from {}: {} bytes", proof_file, proof.len());

    let method = "eth_blockNumber";
    let params = r#"[]"#;
    let chain_id = 1;

    let mut verifier = Verifier::new(
        &proof,
        method,
        params,
        chain_id,
        "",
    )?;
    println!("Verifier created");

    let client = reqwest::Client::new();
    let beacon_api_base = "https://lodestar-mainnet.chainsafe.io";

    loop {
        let status_json = verifier.execute_json_status()?;
        let status: serde_json::Value = serde_json::from_str(&status_json)?;

        match status["status"].as_str() {
            Some("success") => {
                println!("✅ Proof verification SUCCESSFUL!");
                if let Some(result) = status.get("result") {
                    println!("Result: {}", result);
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

    Ok(())
}