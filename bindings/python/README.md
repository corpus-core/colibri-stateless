<img src="https://github.com/corpus-core/colibri-stateless/raw/dev/c4_logo.png" alt="C4 Logo" width="300"/>

# Colibri Python Bindings (corpus core colibri client)

![ETH2.0_Spec_Version 1.4.0](https://img.shields.io/badge/ETH2.0_Spec_Version-1.4.0-2e86c1.svg)

The colibri client is a stateless and trustless ethereum client, which is optimized for the mobile apps or embedded devices, because it does not hols the state, but verifies on demand. 

## 🚀 Quick Start

### Installation

```bash
python3 -m pip install colibri-stateless
```

### Basic Usage

```python
import asyncio
from colibri import Colibri

async def main():
    # Initialize client for Ethereum Mainnet
    client = Colibri(chain_id=1, provers=["https://mainnet.colibri-proof.tech"])
    
    # Make verified RPC call
    result = await client.rpc("eth_blockNumber", [])
    print(f"Current block: {result}")
    
    # Get account balance with proof verification
    balance = await client.rpc("eth_getBalance", [
        "0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5", 
        "latest"
    ])
    print(f"Balance: {balance}")

# Run async function
asyncio.run(main())
```

## ✨ Key Features

- **🔐 Cryptographic Verification** - All RPC responses verified with Merkle proofs
- **🚀 Async/Await Support** - Modern Python async support for network operations  
- **💾 Pluggable Storage** - Customizable storage backends for caching
- **🧪 Comprehensive Testing** - Mock HTTP requests and storage for testing
- **🌐 Multi-Chain Support** - Ethereum Mainnet, Sepolia, Gnosis Chain, and more
- **📦 Easy Integration** - Simple pip install with pre-built native extensions

## 📖 Documentation

**Full Documentation**: [GitBook Guide](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings/python)

- **API Reference** - Complete class and method documentation
- **Storage System** - Custom storage implementations
- **Testing Framework** - Mock data and integration tests  
- **Configuration** - Chain setup and advanced options
- **Building from Source** - Development and contribution guide

## 🛠️ Development

### Building from Source

```bash
# Clone repository
git clone https://github.com/corpus-core/colibri-stateless.git
cd colibri-stateless/bindings/python

# Build native extension
./build.sh

# Option 1: Use virtual environment (recommended)
python3 -m venv venv
source venv/bin/activate
pip install -e .
pip install -r requirements-dev.txt

# Run tests
pytest tests/ -v

# Deactivate when done
deactivate
```

**Alternative without virtual environment:**

```bash
# Install test dependencies with --user flag
python3 -m pip install --user pytest pytest-asyncio aiohttp

# Run tests directly with PYTHONPATH
PYTHONPATH=src python3 -m pytest tests/ -v
```

### Quick Debug Build

For faster iteration during development:

```bash
# Build in debug mode
./build_debug.sh

# Run tests without installation
PYTHONPATH=src python3 -m pytest tests/ -v
```

### Integration Tests

```python
# Run with real blockchain data (offline)
from colibri.testing import discover_tests, run_test_case

tests = discover_tests()
for test_name, test_config in tests.items():
    result = await run_test_case(test_name, test_config)
    print(f"Test {test_name}: {'PASSED' if result else 'FAILED'}")
```

## 📋 System Requirements

- **Python 3.8+**
- **CMake 3.20+** (for building from source)
- **C++17 compiler** (for building from source)

## 🔗 Related Projects

- **Core Library**: [colibri-stateless](https://github.com/corpus-core/colibri-stateless)
- **Swift Bindings**: iOS/macOS native integration
- **Kotlin Bindings**: Android/JVM integration
- **JavaScript Bindings**: Web/Node.js integration

## 📄 License

MIT License - see [LICENSE](../../LICENSE) for details.

## 🤝 Contributing

Contributions welcome! Please read our [Contributing Guide](../../CONTRIBUTING.md) and check the [Development Documentation](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings/python).