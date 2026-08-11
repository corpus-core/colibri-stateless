//! Minimal example: run a verified `eth_blockNumber` call on mainnet.
//!
//! Run with:
//!
//! ```bash
//! cargo run --example rpc
//! ```

use colibri_stateless::{Colibri, ColibriError, MAINNET};
use serde_json::json;

#[tokio::main]
async fn main() -> Result<(), ColibriError> {
    let client = Colibri::builder(MAINNET).build();

    let result = client.rpc("eth_blockNumber", &json!([])).await?;

    match result.as_str() {
        Some(hex) => {
            let n = u64::from_str_radix(hex.trim_start_matches("0x"), 16).unwrap_or(0);
            println!("current block: {hex} ({n})");
        }
        None => println!("result: {result}"),
    }
    Ok(())
}
