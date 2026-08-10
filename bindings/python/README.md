<img src="https://github.com/corpus-core/colibri-stateless/raw/dev/c4_logo.png" alt="Colibri Logo" width="300"/>

# Colibri Stateless — Python

**Verify Ethereum RPC data cryptographically — without running a full node.**

![ETH2.0 Spec Version 1.4.0](https://img.shields.io/badge/ETH2.0_Spec_Version-1.4.0-2e86c1.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)
[![PyPI](https://img.shields.io/pypi/v/colibri-stateless.svg)](https://pypi.org/project/colibri-stateless/)

Colibri Stateless is a highly efficient prover/verifier for Ethereum (with upcoming support for Layer-2s such as OP-Stack). These Python bindings wrap the C core and give you an async API that verifies every RPC response against the beacon chain — no full node, no continuous sync.

[**Website**](https://www.corpuscore.tech/colibri) · [**Docs**](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings/python) · [**Whitepaper**](https://corpus-core.gitbook.io/whitepaper-colibri-stateless) · [**Privacy (PAP)**](https://corpus-core.gitbook.io/pap-colibri-stateless)

## Why Colibri?

- **Stateless** — verification needs nothing but the proof and the sync committee it is checked against. The committee is cached locally so it does not have to travel with every request, but it works just as well with an empty cache or none at all. No persistent state, no full node.
- **Cryptographically verified RPC** — every RPC response is checked against BLS signatures.
- **Offline verification** — proofs are fully self-contained and verify without any network connection, thanks to zk-proofs for the sync committee and signed checkpoints.
- **On-demand, not always-on** — work happens only when you make a request; no background sync burning bandwidth, CPU, or battery.
- **Verifies historical data (older than ~27h / 8192 blocks)** — via `historical_summaries` proofs, where other light clients simply fail.
- **`eth_getLogs` completeness proofs** — optional `logs_completeness=True` proves no matching log was omitted in the requested range.
- **Fully verified local transaction simulation** — simulate a transaction against verified state before signing.
- **Privacy-aware** — Pragmatic Adaptive Privacy (PAP) mode (`privacy_mode=PrivacyMode.BASIC`). *Experimental.*

## Quick Start

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

## Python-specific features

- **Async/await** — modern async API for all network operations.
- **Pluggable storage** — customizable storage backends for caching.
- **Easy integration** — `pip install` with pre-built native extensions.
- **Testing utilities** — mock HTTP requests and storage for deterministic tests.
- **Multi-chain** — Ethereum Mainnet, Sepolia, Gnosis Chain, and more.
- **Local `eth_call` proofs** — `use_accesslist=True` (default) uses `eth_createAccessList`; set `False` for legacy `debug_traceCall`.
- **Privacy-preserving `eth_call`** — combine `ProverMode.HYBRID` + `PrivacyMode.BASIC` + `oblivious_nodes` (default empty; e.g. `https://rpc.safe-node.com/`, API key for testing). Setting `oblivious_nodes` auto-enables PAP. TEE/ORAM background: [Oblivious Labs](https://www.obliviouslabs.com/).

## Documentation

**Full documentation**: [GitBook Guide](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings/python)

- **API Reference** — complete class and method documentation
- **Storage System** — custom storage implementations
- **Testing Framework** — mock data and integration tests
- **Configuration** — chain setup, `use_accesslist` / prover mode, and other advanced options
- **Building from Source** — development and contribution guide

## Development

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

## System Requirements

- **Python 3.8+**
- **CMake 3.20+** (for building from source)
- **C++17 compiler** (for building from source)

## Related Projects

- **Core Library**: [colibri-stateless](https://github.com/corpus-core/colibri-stateless)
- **Swift Bindings**: iOS/macOS native integration
- **Kotlin Bindings**: Android/JVM integration
- **JavaScript Bindings**: Web/Node.js integration

## License

MIT License - see [LICENSE](../../LICENSE) for details.

## Contributing

Contributions welcome! Please read our [Contributing Guide](../../CONTRIBUTING.md) and check the [Development Documentation](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings/python).