use colibri::{ColibriClient, MemoryStorage, Storage, Result};

#[tokio::main]
async fn main() -> Result<()> {
    println!("Testing Colibri with custom storage...");

    let storage = Some(MemoryStorage::new() as Box<dyn Storage>);

    let client = ColibriClient::new(
        Some("https://lodestar-mainnet.chainsafe.io".to_string()),
        Some("https://rpc.ankr.com/eth".to_string()),
        storage,
    );

    println!("Client created with memory storage");

    let method = "eth_blockNumber";
    let params = "[]";
    let chain_id = 1;
    let flags = 0;

    println!("Generating proof for {}...", method);
    match client.prove(method, params, chain_id, flags).await {
        Ok(proof) => {
            println!("Proof generated: {} bytes", proof.len());

            println!("Verifying proof...");
            match client.verify(&proof, method, params, chain_id, "").await {
                Ok(result) => {
                    println!("Verification successful");
                    if let Some(block_num) = result.as_str() {
                        println!("Block number: {}", block_num);
                    }
                }
                Err(e) => {
                    println!("Verification failed: {}", e);
                }
            }
        }
        Err(e) => {
            println!("Proof generation failed: {}", e);
        }
    }

    Ok(())
}
