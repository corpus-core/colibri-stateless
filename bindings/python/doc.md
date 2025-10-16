: Bindings

:: Python

Python bindings for the Colibri stateless Ethereum proof library. Generate and verify cryptographic proofs for Ethereum RPC calls without trusting centralized infrastructure.

## Overview

The Colibri Python Bindings provide a modern, async-first Python API for verified blockchain interactions. Built with pybind11 for optimal performance and memory management, these bindings enable secure Web3 functionality without dependency on centralized RPC providers.

### Core Features

- **🔐 Cryptographic Verification** - All RPC responses validated with Merkle proofs
- **🚀 Async/Await Support** - Modern Python async support for network operations
- **💾 Pluggable Storage** - Customizable storage backends for caching
- **🧪 Comprehensive Testing** - Mock HTTP requests and storage for testing
- **🌐 Multi-Chain Support** - Ethereum Mainnet, Sepolia, Gnosis Chain, and more
- **📦 Easy Installation** - Simple pip install with pre-built native extensions

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    Python Application Layer                     │
├─────────────────────────────────────────────────────────────────┤
│                     colibri.client API                          │
│  • Colibri class (main interface)                               │
│  • Async RPC methods                                            │
│  • Storage & HTTP abstractions                                  │
│  • Error handling & type conversion                             │
├─────────────────────────────────────────────────────────────────┤
│                  Python-C++ Bridge Layer                        │
│  • _native.so (pybind11 extension)                              │
│  • Function pointer callbacks                                   │
│  • Memory management & cleanup                                  │
├─────────────────────────────────────────────────────────────────┤
│                   Core C Libraries                              │
│  • Prover (proof generation)                                   │
│  • Verifier (proof verification)                                │
│  • Storage plugin system                                        │
│  • Cryptographic libraries (blst, ed25519)                      │
└─────────────────────────────────────────────────────────────────┘
```

## Installation

### PyPI Installation (Recommended)

```bash
pip install colibri-stateless
```

Pre-built wheels are available for:
- **Linux**: x86_64 
- **macOS**: ARM64 (Apple Silicon) and x86_64 (Intel)
- **Windows**: x86_64

### Development Installation

```bash
# Clone repository
git clone https://github.com/corpus-core/colibri-stateless.git
cd colibri-stateless/bindings/python

# Build from source
./build.sh

# Install in development mode
pip install -e .
```

## Quick Start

### Basic RPC Calls

```python
import asyncio
from colibri import Colibri

async def main():
    # Initialize client for Ethereum Mainnet
    client = Colibri(chain_id=1, provers=["https://mainnet.colibri-proof.tech"])
    
    # Get current block number
    block_number = await client.rpc("eth_blockNumber", [])
    print(f"Current block: {int(block_number, 16)}")
    
    # Get account balance
    balance = await client.rpc("eth_getBalance", [
        "0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5", 
        "latest"
    ])
    print(f"Balance: {int(balance, 16) / 10**18} ETH")

asyncio.run(main())
```

### Local Proof Generation

```python
import asyncio
from colibri import Colibri

async def main():
    # Force local proof generation (no remote provers)
    client = Colibri(
        chain_id=1,
        provers=[],  # Empty = local proof generation
        eth_rpcs=["https://eth.llamarpc.com"],
        beacon_apis=["https://lodestar-mainnet.chainsafe.io"]
    )
    
    # This will generate proof locally
    result = await client.rpc("eth_getProof", [
        "0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5",
        ["0x0"],
        "latest"
    ])
    
    print("Proof generated and verified locally!")

asyncio.run(main())
```

### Multi-Chain Setup

```python
import asyncio
from colibri import Colibri

class MultiChainClient:
    def __init__(self):
        self.clients = {}
    
    def get_client(self, chain_id: int) -> Colibri:
        if chain_id not in self.clients:
            self.clients[chain_id] = Colibri(
                chain_id=chain_id,
                provers=["https://mainnet.colibri-proof.tech"]
            )
        return self.clients[chain_id]
    
    async def get_balance(self, account: str, chain_id: int) -> str:
        client = self.get_client(chain_id)
        return await client.rpc("eth_getBalance", [account, "latest"])

async def main():
    multi = MultiChainClient()
    
    # Ethereum Mainnet
    eth_balance = await multi.get_balance(
        "0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5", 1
    )
    
    # Polygon
    polygon_balance = await multi.get_balance(
        "0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5", 137
    )
    
    print(f"ETH Balance: {eth_balance}")
    print(f"Polygon Balance: {polygon_balance}")

asyncio.run(main())
```

## API Reference

### Colibri Class

```python
class Colibri:
    def __init__(
        self,
        chain_id: int = 1,
        provers: Optional[List[str]] = None,
        eth_rpcs: Optional[List[str]] = None,
        beacon_apis: Optional[List[str]] = None,
        trusted_block_hashes: Optional[List[str]] = None,
        request_handler: Optional[RequestHandler] = None,
        storage: Optional[ColibriStorage] = None
    ):
        """
        Initialize Colibri client.
        
        Args:
            chain_id: Blockchain chain ID (1=Ethereum, 137=Polygon, etc.)
            provers: Remote prover URLs (empty list = local proof generation)
            eth_rpcs: Ethereum RPC endpoints for execution layer
            beacon_apis: Beacon chain API endpoints
            trusted_block_hashes: Trusted block hashes for anchoring
            request_handler: Custom HTTP request handler
            storage: Custom storage implementation
        """
```

#### Core Methods

```python
async def rpc(self, method: str, params: List[Any]) -> Any:
    """
    Execute RPC call with automatic proof verification.
    
    Args:
        method: RPC method name (e.g., "eth_getBalance")
        params: Method parameters as list
        
    Returns:
        Verified RPC response
        
    Raises:
        ColibriError: If proof generation/verification fails
        RPCError: If RPC call fails
        HTTPError: If network request fails
    """

def get_method_support(self, method: str) -> MethodType:
    """
    Check support level for an RPC method.
    
    Returns:
        MethodType.LOCAL: Supported locally
        MethodType.REMOTE: Requires remote prover
        MethodType.UNSUPPORTED: Not supported
    """
```

### Storage System

```python
from abc import ABC, abstractmethod
from typing import Optional, List

class ColibriStorage(ABC):
    """Abstract base class for storage implementations."""
    
    @abstractmethod
    def get(self, key: str) -> Optional[bytes]:
        """Retrieve data by key."""
        pass
    
    @abstractmethod
    def set(self, key: str, value: bytes) -> None:
        """Store data with key."""
        pass
    
    @abstractmethod
    def delete(self, key: str) -> None:
        """Delete data by key."""
        pass
    
    @abstractmethod
    def list_keys(self) -> List[str]:
        """List all stored keys."""
        pass
```

### Built-in Storage Implementations

#### Default File Storage

```python
from colibri.storage import DefaultStorage

# Uses C4_STATES_DIR environment variable or current directory
storage = DefaultStorage()
client = Colibri(chain_id=1, storage=storage)
```

#### Memory Storage

```python
from colibri.storage import MemoryStorage

# In-memory storage (lost on restart)
storage = MemoryStorage()
client = Colibri(chain_id=1, storage=storage)
```

#### Custom Storage Implementation

```python
import sqlite3
from colibri.storage import ColibriStorage

class SQLiteStorage(ColibriStorage):
    def __init__(self, db_path: str):
        self.db_path = db_path
        with sqlite3.connect(db_path) as conn:
            conn.execute("""
                CREATE TABLE IF NOT EXISTS colibri_storage (
                    key TEXT PRIMARY KEY,
                    value BLOB
                )
            """)
    
    def get(self, key: str) -> Optional[bytes]:
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.execute("SELECT value FROM colibri_storage WHERE key = ?", (key,))
            row = cursor.fetchone()
            return row[0] if row else None
    
    def set(self, key: str, value: bytes) -> None:
        with sqlite3.connect(self.db_path) as conn:
            conn.execute(
                "INSERT OR REPLACE INTO colibri_storage (key, value) VALUES (?, ?)",
                (key, value)
            )
    
    def delete(self, key: str) -> None:
        with sqlite3.connect(self.db_path) as conn:
            conn.execute("DELETE FROM colibri_storage WHERE key = ?", (key,))
    
    def list_keys(self) -> List[str]:
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.execute("SELECT key FROM colibri_storage")
            return [row[0] for row in cursor.fetchall()]

# Usage
storage = SQLiteStorage("colibri.db")
client = Colibri(chain_id=1, storage=storage)
```

## Testing Framework

### Mock Testing

```python
from colibri.testing import MockStorage, MockRequestHandler
from colibri import Colibri

# Create mock storage with test data
mock_storage = MockStorage({
    "states_1": b'{"sync_committee": "..."}',
    "validators_1_12345": b'{"validators": [...]}' 
})

# Create mock HTTP handler
mock_requests = MockRequestHandler({
    "eth_getBalance": '{"result": "0x1bc16d674ec80000"}',
    "eth_blockNumber": '{"result": "0x12a05f1"}'
})

# Test with mocks
client = Colibri(
    chain_id=1,
    provers=[],  # Force local testing
    storage=mock_storage,
    request_handler=mock_requests
)

result = await client.rpc("eth_getBalance", ["0x742d35...", "latest"])
assert result == "0x1bc16d674ec80000"
```

### Integration Testing

```python
import asyncio
from colibri.testing import discover_tests, run_test_case

async def run_integration_tests():
    """Run all integration tests from test/data directory."""
    tests = discover_tests()
    passed = 0
    failed = 0
    
    for test_name, test_config in tests.items():
        try:
            result = await run_test_case(test_name, test_config)
            if result:
                print(f"PASS: {test_name}")
                passed += 1
            else:
                print(f"FAIL: {test_name}")
                failed += 1
        except Exception as e:
            print(f"ERROR: {test_name} - {e}")
            failed += 1
    
    print(f"\nResults: {passed} passed, {failed} failed")

# Run tests
asyncio.run(run_integration_tests())
```

### Custom Test Data

```python
from colibri.testing import FileBasedMockStorage, FileBasedMockRequestHandler

# Load test data from custom directory
mock_storage = FileBasedMockStorage("my_test_data/storage/")
mock_requests = FileBasedMockRequestHandler("my_test_data/requests/")

client = Colibri(
    chain_id=1,
    provers=[],
    storage=mock_storage,
    request_handler=mock_requests
)
```

## Configuration

### Chain Configuration

```python
# Supported chains
ETHEREUM_MAINNET = 1
ETHEREUM_SEPOLIA = 11155111
POLYGON = 137
GNOSIS = 100
ARBITRUM = 42161
BASE = 8453
OPTIMISM = 10

# Configure for specific chain
client = Colibri(
    chain_id=POLYGON,
    provers=["https://polygon.colibri-proof.tech"],
    eth_rpcs=["https://polygon-rpc.com"],
    beacon_apis=["https://polygon-beacon.example.com"]
)
```

### Advanced Configuration

```python
from colibri import Colibri

client = Colibri(
    chain_id=1,
    
    # Remote proof generation (faster, requires trust)
    provers=["https://mainnet.colibri-proof.tech"],
    
    # Local proof generation (slower, trustless)
    eth_rpcs=[
        "https://eth.llamarpc.com",
        "https://rpc.ankr.com/eth",
        "https://ethereum.publicnode.com"
    ],
    beacon_apis=[
        "https://lodestar-mainnet.chainsafe.io",
        "https://mainnet.beaconstate.info",
        "https://beaconcha.in/api/v1/client/events"
    ],
    
    # Trusted anchoring points
    trusted_block_hashes=[
        "0x4232db57354ddacec40adda0a502f7732ede19ba0687482a1e15ad20e5e7d1e7"
    ],
    
    # Custom implementations
    storage=MyCustomStorage(),
    request_handler=MyCustomRequestHandler()
)
```

## Error Handling

### Exception Types

```python
from colibri.types import (
    ColibriError,      # Base exception
    ProofError,        # Proof generation/verification failed
    VerificationError, # Proof verification failed
    RPCError,          # RPC call failed
    HTTPError          # Network request failed
)

try:
    result = await client.rpc("eth_getBalance", ["0x...", "latest"])
except ProofError as e:
    print(f"Proof error: {e}")
    # Handle proof generation failure
except VerificationError as e:
    print(f"Verification failed: {e}")
    # Handle proof verification failure
except RPCError as e:
    print(f"RPC error: {e}")
    # Handle RPC call failure
except HTTPError as e:
    print(f"Network error: {e}")
    # Handle network issues
except ColibriError as e:
    print(f"General Colibri error: {e}")
    # Handle any other Colibri error
```

### Graceful Degradation

```python
async def safe_rpc_call(client: Colibri, method: str, params: List[Any]):
    """Make RPC call with fallback to unverified provider."""
    try:
        # Try verified call first
        return await client.rpc(method, params)
    except (ProofError, VerificationError) as e:
        print(f"Verification failed: {e}")
        
        # Fallback to unverified RPC (use with caution!)
        if hasattr(client, 'fallback_rpc'):
            print("Falling back to unverified RPC")
            return await client.fallback_rpc(method, params)
        else:
            raise
```

## Building from Source

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install cmake build-essential python3-dev

# macOS
brew install cmake

# Windows (with Chocolatey)
choco install cmake visualstudio2022buildtools
```

### Build Process

```bash
# Clone repository
git clone https://github.com/corpus-core/colibri-stateless.git
cd colibri-stateless/bindings/python

# Build native extension
./build.sh

# Install in development mode
pip install -e .

# Verify installation
python -c "import colibri; print('Import successful')"
```

### Development Build

```bash
# Debug build with symbols
./build.sh --debug

# Clean build
./build.sh --clean

# Build with specific Python version
./build.sh --python python3.11
```

### Running Tests

```bash
# Unit tests
python -m pytest tests/ -v

# Integration tests  
python scripts/run_integration_tests.py

# Specific test
python -m pytest tests/test_client.py::test_basic_rpc -v

# With coverage
python -m pytest tests/ --cov=colibri --cov-report=html
```

## Performance Optimization

### Connection Pooling

```python
import aiohttp
from colibri.client import RequestHandler

class PooledRequestHandler(RequestHandler):
    def __init__(self):
        self.session = None
    
    async def get_session(self):
        if self.session is None:
            connector = aiohttp.TCPConnector(
                limit=100,           # Total connection pool size
                limit_per_host=30,   # Per-host connection limit
                keepalive_timeout=60 # Keep connections alive
            )
            self.session = aiohttp.ClientSession(connector=connector)
        return self.session
    
    async def handle_request(self, request):
        session = await self.get_session()
        async with session.post(
            request.url,
            json=request.payload,
            headers={"Content-Type": "application/json"}
        ) as response:
            return await response.read()

# Usage
client = Colibri(
    chain_id=1,
    request_handler=PooledRequestHandler()
)
```

### Storage Caching

```python
from functools import lru_cache
from colibri.storage import ColibriStorage

class CachedStorage(ColibriStorage):
    def __init__(self, underlying: ColibriStorage, cache_size: int = 1000):
        self.underlying = underlying
        self.cache_size = cache_size
        # Use functools.lru_cache for automatic LRU eviction
        self._cached_get = lru_cache(maxsize=cache_size)(self._raw_get)
    
    def _raw_get(self, key: str) -> Optional[bytes]:
        return self.underlying.get(key)
    
    def get(self, key: str) -> Optional[bytes]:
        return self._cached_get(key)
    
    def set(self, key: str, value: bytes) -> None:
        self.underlying.set(key, value)
        # Update cache
        self._cached_get.cache_clear()  # Simple invalidation
        self._cached_get(key)  # Pre-populate

# Usage
base_storage = DefaultStorage()
cached_storage = CachedStorage(base_storage, cache_size=500)
client = Colibri(chain_id=1, storage=cached_storage)
```

## Troubleshooting

### Common Issues

#### Import Error: "No module named '_native'"

```bash
# Solution: Rebuild native extension
cd bindings/python
./build.sh
pip install -e .

# Verify build
python -c "import colibri._native; print('Native module loaded')"
```

#### "Segmentation fault on exit"

This was a known issue with Python/C++ object lifetime. Fixed in current version.

```python
# Workaround for older versions: explicit cleanup
import atexit
from colibri import Colibri

client = Colibri(chain_id=1)

def cleanup():
    # Force cleanup before exit
    del client

atexit.register(cleanup)
```

#### RPC Calls Fail with Proof Errors

```python
try:
    result = await client.rpc("eth_blockNumber", [])
except ProofError as e:
    print(f"Proof error: {e}")
    
    # Check if method is supported
    support = client.get_method_support("eth_blockNumber")
    print(f"Method support: {support}")
    
    # Try with remote prover
    client_remote = Colibri(
        chain_id=1,
        provers=["https://mainnet.colibri-proof.tech"]
    )
    result = await client_remote.rpc("eth_blockNumber", [])
```

#### Windows Build Issues

```bash
# Install Visual Studio Build Tools
choco install visualstudio2022buildtools --package-parameters "--add Microsoft.VisualStudio.Workload.VCTools"

# Set environment
set CMAKE_GENERATOR="Visual Studio 17 2022"

# Build
python build.py  # Alternative to build.sh on Windows
```

### Debug Mode

```python
import logging
from colibri import Colibri

# Enable debug logging
logging.basicConfig(level=logging.DEBUG)

client = Colibri(chain_id=1)

# This will show detailed debug information
result = await client.rpc("eth_blockNumber", [])
```

### Memory Usage Monitoring

```python
import psutil
import asyncio
from colibri import Colibri

async def monitor_memory():
    client = Colibri(chain_id=1)
    process = psutil.Process()
    
    print(f"Initial memory: {process.memory_info().rss / 1024 / 1024:.1f} MB")
    
    for i in range(100):
        result = await client.rpc("eth_blockNumber", [])
        if i % 10 == 0:
            memory = process.memory_info().rss / 1024 / 1024
            print(f"After {i} calls: {memory:.1f} MB")

asyncio.run(monitor_memory())
```

## Platform Specifics

### Linux Considerations

- **glibc version**: Pre-built wheels require glibc 2.28+ (Ubuntu 20.04+)
- **Security**: Runs in user space, no special permissions required
- **Performance**: Native performance with direct C++ integration

### macOS Considerations

- **Apple Silicon**: Native ARM64 support with optimal performance
- **Intel Macs**: x86_64 compatibility maintained
- **Code Signing**: All native libraries are properly signed
- **Minimum Version**: macOS 10.15+ (Catalina)

### Windows Considerations

- **Unicode**: Full UTF-8 support for all text operations
- **Path Length**: Handles long file paths correctly
- **Permissions**: No administrator privileges required
- **Minimum Version**: Windows 10 (1809+)

## CI/CD Integration

### GitHub Actions

```yaml
name: Python Integration Test

on: [push, pull_request]

jobs:
  test:
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest, windows-latest]
        python-version: ['3.8', '3.9', '3.10', '3.11', '3.12']
    
    steps:
    - uses: actions/checkout@v4
    
    - name: Set up Python ${{ matrix.python-version }}
      uses: actions/setup-python@v4
      with:
        python-version: ${{ matrix.python-version }}
    
    - name: Install dependencies
      run: |
        python -m pip install --upgrade pip
        pip install colibri pytest pytest-asyncio
    
    - name: Run tests
      run: |
        pytest tests/ -v
```

### Docker Integration

```dockerfile
FROM python:3.11-slim

# Install system dependencies
RUN apt-get update && apt-get install -y \
    cmake \
    build-essential \
    && rm -rf /var/lib/apt/lists/*

# Install Colibri
COPY . /app
WORKDIR /app
RUN pip install -e .

# Run application
CMD ["python", "your_app.py"]
```

## Further Information

- **📖 Online Documentation**: [GitBook Guide](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings/python)
- **Core Repository**: [colibri-stateless](https://github.com/corpus-core/colibri-stateless)
- **Issue Tracker**: [GitHub Issues](https://github.com/corpus-core/colibri-stateless/issues)
- **API Reference**: [Python API Documentation](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings/python#api-reference)
- **Contributing Guide**: [Development Documentation](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/building)