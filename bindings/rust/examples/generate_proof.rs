use colibri::{ProverClient, Result};

#[tokio::main]
async fn main() -> Result<()> {
    let method = "eth_blockNumber";
    let params = r#"[]"#;
    let chain_id = 1;
    let flags = 0;

    println!("Generating proof for {}...", method);

    let beacon_api_url = "https://lodestar-mainnet.chainsafe.io".to_string();
    let eth_rpc_url = "https://ethereum-rpc.publicnode.com".to_string();

    let mut client = ProverClient::with_urls(
        method,
        params,
        chain_id,
        flags,
        Some(beacon_api_url),
        Some(eth_rpc_url),
    )?;

    let proof = client.run_to_completion().await?;

    std::fs::write("proof.bin", &proof)
        .expect("Failed to write proof to file");
    println!("✅ Proof saved to proof.bin ({} bytes)", proof.len());

    let hex = proof.iter().map(|b| format!("{:02x}", b)).collect::<String>();
    println!("First 64 bytes (hex): {}", &hex[..128.min(hex.len())]);

    Ok(())
}