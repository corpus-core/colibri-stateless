use colibri::{ProverClient, Result};

#[tokio::main]
async fn main() -> Result<()> {
    // Example: Get account balance
    let method = "eth_getBalance";
    let params = r#"["0x742d35Cc6634C0532925a3b844Bc9e7595f0bEb", "latest"]"#;
    let chain_id = 1; // Ethereum mainnet
    let flags = 0;

    let mut client = ProverClient::new(method, params, chain_id, flags)?;

    println!("Starting proof generation for {}...", method);

    let proof = client.run_to_completion_concurrent().await?;

    println!("Proof generated! Size: {} bytes", proof.len());

    // Verify the proof
    // let mut verifier = Verifier::new(&proof, method, params, chain_id, "trusted_checkpoint")?;
    // let result = verifier.execute_json_status()?;
    // println!("Verification result: {}", result);

    Ok(())
}