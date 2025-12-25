<img src="https://github.com/corpus-core/colibri-stateless/raw/dev/c4_logo.png" alt="C4 Logo" width="300"/>

# Colibri Rust Bindings

![ETH2.0_Spec_Version 1.4.0](https://img.shields.io/badge/ETH2.0_Spec_Version-1.4.0-2e86c1.svg)

Rust bindings for Colibri - a stateless and trustless Ethereum light client optimized for resource-constrained environments. Provides cryptographic proof generation and verification without holding state.

## 🚀 Quick Start

### Installation

Add to your `Cargo.toml`:

```toml
[dependencies]
colibri = { path = "path/to/colibri/bindings/rust" }
tokio = { version = "1.0", features = ["full"] }
```

### Basic Usage

```rust
use colibri::{ColibriClient, Result};

#[tokio::main]
async fn main() -> Result<()> {
    // Initialize client with beacon and RPC endpoints
    let client = ColibriClient::with_urls(
        Some("https://lodestar-mainnet.chainsafe.io".to_string()),
        Some("https://ethereum-rpc.publicnode.com".to_string()),
    );

    // Generate a cryptographic proof
    let proof = client.prove("eth_blockNumber", "[]", 1, 0).await?;
    println!("Proof generated: {} bytes", proof.len());

    // Verify the proof and get result
    let result = client.verify(&proof, "eth_blockNumber", "[]", 1, "").await?;
    println!("Verified result: {}", result);

    Ok(())
}
```

## ✨ Key Features

- **🔐 Cryptographic Verification** - All RPC responses verified with Merkle proofs
- **⚡ Async/Await Support** - Modern async Rust for efficient network operations
- **🦀 Memory Safe** - Leverages Rust's ownership system for safety
- **🔧 Zero-Copy FFI** - Efficient C library integration
- **🌐 Multi-Chain Support** - Ethereum Mainnet, Sepolia, Gnosis Chain, and more
- **📦 Native Performance** - Direct access to optimized C++ implementation

## 📖 Documentation

**Full Documentation**: [GitBook Guide](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings/rust)

- **API Reference** - Complete struct and method documentation
- **Supported RPC Methods** - Full list of available Ethereum RPC calls
- **Integration Guide** - Best practices for production use
- **Building from Source** - Development and contribution guide

## 🛠️ API Reference

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
- `eth_getTransactionByHash` - Get transaction details
- `eth_getTransactionReceipt` - Get transaction receipt
- `eth_call` - Execute a call without creating a transaction
- And more...

Full list available in the [documentation](https://corpus-core.gitbook.io/specification-colibri-stateless/specifications/ethereum/supported-rpc-methods).

## 🔨 Development

### Building from Source

```bash
# Clone repository
git clone https://github.com/corpus-core/colibri-stateless.git
cd colibri-stateless

# Build C++ library first
./build.sh

# Build Rust bindings
cd bindings/rust
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

### Running Tests

```bash
# Unit tests
cargo test --lib

# Integration tests
cargo test --test '*'

# Run with verbose output
cargo test -- --nocapture

# Run specific test
cargo test test_method_support
```

### Example

A complete working example is available in `examples/colibri_example.rs`:

```bash
cargo run --example colibri_example
```

## 📋 System Requirements

- **Rust 1.70+** - Modern async/await support
- **CMake 3.20+** - For building C++ library
- **C++17 compiler** - For native library compilation
- **OpenSSL** - For cryptographic operations

## 🔗 Related Projects

- **Core Library**: [colibri-stateless](https://github.com/corpus-core/colibri-stateless)
- **Python Bindings**: Python async integration
- **Swift Bindings**: iOS/macOS native integration
- **Kotlin Bindings**: Android/JVM integration
- **JavaScript Bindings**: Web/Node.js integration

## 📄 License

MIT License - see [LICENSE](../../LICENSE) for details.

## 🤝 Contributing

Contributions welcome! Please read our [Contributing Guide](../../CONTRIBUTING.md) and check the [Development Documentation](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings/rust).

### Development Guidelines

1. Follow Rust best practices and idioms
2. Maintain compatibility with the C library API
3. Add tests for new functionality
4. Update documentation for API changes
5. Run `cargo fmt` and `cargo clippy` before submitting