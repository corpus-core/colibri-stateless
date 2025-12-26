use colibri::{ColibriClient, Result};

#[tokio::main]
async fn main() -> Result<()> {
    let method = "eth_blockNumber";
    let params = "[]";
    let chain_id = 1;
    let flags = 0;

    let client = ColibriClient::new(
        Some("https://lodestar-mainnet.chainsafe.io".to_string()),
        Some("https://ethereum-rpc.publicnode.com".to_string()),
        None,
    );

    // Generate proof
    println!("Generating proof for {}...", method);
    let proof = client.prove(method, params, chain_id, flags).await?;
    println!("Proof generated: {} bytes", proof.len());

    // Verify proof
    println!("\nVerifying proof...");
    let result = client.verify(&proof, method, params, chain_id, "").await?;
    println!("Verification successful");

    // Display result
    if let Some(block_num) = result.as_str() {
        let hex_num = block_num.trim_start_matches("0x");
        if let Ok(num) = u64::from_str_radix(hex_num, 16) {
            println!("Block number: {} ({})", block_num, num);
        } else {
            println!("Result: {}", block_num);
        }
    } else {
        println!("Result: {}", serde_json::to_string_pretty(&result)?);
    }

    Ok(())
}