# Colibri Rust Bindings

Rust bindings for Colibri - a stateless Ethereum light client for proof generation and verification.

## Installation

Add to your Cargo.toml:
```toml
[dependencies]
colibri = { path = "path/to/colibri/bindings/rust" }
tokio = { version = "1.0", features = ["full"] }
```

## Quick Start

```rust
use colibri::{ColibriClient, Result};

#[tokio::main]
async fn main() -> Result<()> {
    let client = ColibriClient::with_urls(
        Some("https://lodestar-mainnet.chainsafe.io".to_string()),
        Some("https://ethereum-rpc.publicnode.com".to_string()),
    );

    // Generate a proof
    let proof = client.prove("eth_blockNumber", "[]", 1, 0).await?;
    println!("Proof generated: {} bytes", proof.len());

    // Verify the proof
    let result = client.verify(&proof, "eth_blockNumber", "[]", 1, "").await?;
    println!("Verified result: {}", result);

    Ok(())
}
```

## API Reference

### ColibriClient

The main client for interacting with Colibri.

#### Methods

- `new()` - Create a client without URLs
- `with_urls(beacon_api_url, eth_rpc_url)` - Create a client with specified URLs
- `prove(method, params, chain_id, flags)` - Generate a cryptographic proof
- `verify(proof, method, params, chain_id, checkpoint)` - Verify a proof and get the result

### Supported Methods

- `eth_blockNumber` - Get the latest block number
- `eth_getBalance` - Get account balance
- `eth_getBlockByNumber` - Get block by number
- `eth_getBlockByHash` - Get block by hash
- And more...

## Building from Source

```bash
# Build the library
cargo build --release

# Run the example
cargo run --example colibri_example

# Run tests
cargo test

# Format code
cargo fmt

# Check for issues
cargo clippy
```

## Requirements

- Rust 1.70 or later
- Access to Ethereum RPC endpoint
- Access to Beacon Chain API endpoint

## Example

See `examples/colibri_example.rs` for a complete working example.

## License

MIT