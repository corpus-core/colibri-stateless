//! Example: run a verified RPC call with a custom in-memory storage
//! backend.
//!
//! Run with:
//!
//! ```bash
//! cargo run --example storage
//! ```

use colibri_stateless::{Colibri, ColibriError, MemoryStorage, MAINNET};
use serde_json::json;

// `current_thread` keeps the example portable across native and wasm
// targets (wasm has no `rt-multi-thread` feature).
#[tokio::main(flavor = "current_thread")]
async fn main() -> Result<(), ColibriError> {
    let client = Colibri::builder(MAINNET)
        .storage(MemoryStorage::new())
        .build();

    let balance = client
        .rpc(
            "eth_getBalance",
            &json!(["0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045", "latest"]),
        )
        .await?;

    println!("balance: {balance}");
    Ok(())
}
