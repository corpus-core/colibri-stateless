# Colibri Python Bindings

Python bindings for the Colibri stateless Ethereum proof library using pybind11.

## Features

- 🔐 **Proof Generation**: Create cryptographic proofs for Ethereum RPC calls
- ✅ **Proof Verification**: Verify proofs locally without trusted setup
- 🌐 **Multi-Chain Support**: Ethereum Mainnet, Sepolia, Gnosis Chain, and more
- 🚀 **Async/Await**: Modern Python async support for network operations
- 💾 **Custom Storage**: Pluggable storage backend for caching
- 🧪 **Test Support**: Mock HTTP requests and storage for testing

## Installation

### Prerequisites

- Python 3.8+
- CMake 3.20+
- C++17 compatible compiler
- pybind11

### Build from Source

```bash
# Build the native module
./build.sh

# Install in development mode
pip install -e .
```

## Quick Start

```python
import asyncio
from colibri import Colibri, DefaultStorage

async def main():
    # Initialize with custom configuration
    client = Colibri(
        chain_id=1,  # Ethereum Mainnet
        eth_rpcs=["https://rpc.ankr.com/eth"],
        beacon_apis=["https://lodestar-mainnet.chainsafe.io"],
        storage=DefaultStorage()
    )
    
    # Execute a proofable RPC call
    result = await client.rpc("eth_getBalance", [
        "0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5",
        "latest"
    ])
    print(f"Balance: {result}")

if __name__ == "__main__":
    asyncio.run(main())
```

## API Reference

### Colibri Class

#### Constructor
```python
Colibri(
    chain_id: int = 1,
    proofers: List[str] = ["https://c4.incubed.net"],
    eth_rpcs: List[str] = ["https://rpc.ankr.com/eth"],
    beacon_apis: List[str] = ["https://lodestar-mainnet.chainsafe.io"],
    trusted_block_hashes: List[str] = [],
    include_code: bool = False,
    storage: Optional[ColibriStorage] = None
)
```

#### Methods

- `async rpc(method: str, params: List[Any]) -> Any`: Execute RPC call with proof
- `async create_proof(method: str, params: List[Any]) -> bytes`: Create proof manually
- `async verify_proof(proof: bytes, method: str, params: List[Any]) -> Any`: Verify proof manually
- `get_method_support(method: str) -> MethodType`: Check method support

### Storage Interface

```python
class ColibriStorage:
    def get(self, key: str) -> Optional[bytes]: ...
    def set(self, key: str, value: bytes) -> None: ...
    def delete(self, key: str) -> None: ...
```

### Method Types

- `MethodType.PROOFABLE`: Method supports proof generation
- `MethodType.UNPROOFABLE`: Method doesn't support proofs, direct RPC call
- `MethodType.LOCAL`: Local verification only
- `MethodType.NOT_SUPPORTED`: Method not supported

## Examples

### Custom Storage Implementation

```python
import sqlite3
from colibri import ColibriStorage

class SQLiteStorage(ColibriStorage):
    def __init__(self, db_path: str):
        self.db_path = db_path
        self._init_db()
    
    def _init_db(self):
        conn = sqlite3.connect(self.db_path)
        conn.execute('''
            CREATE TABLE IF NOT EXISTS storage (
                key TEXT PRIMARY KEY,
                value BLOB
            )
        ''')
        conn.commit()
        conn.close()
    
    def get(self, key: str) -> Optional[bytes]:
        conn = sqlite3.connect(self.db_path)
        cursor = conn.execute('SELECT value FROM storage WHERE key = ?', (key,))
        row = cursor.fetchone()
        conn.close()
        return row[0] if row else None
    
    def set(self, key: str, value: bytes) -> None:
        conn = sqlite3.connect(self.db_path)
        conn.execute('INSERT OR REPLACE INTO storage (key, value) VALUES (?, ?)', (key, value))
        conn.commit()
        conn.close()
    
    def delete(self, key: str) -> None:
        conn = sqlite3.connect(self.db_path)
        conn.execute('DELETE FROM storage WHERE key = ?', (key,))
        conn.commit()
        conn.close()

# Use custom storage
client = Colibri(storage=SQLiteStorage("colibri.db"))
```

### Testing with Mocks

```python
import pytest
from colibri import Colibri, MockStorage, MockRequestHandler

@pytest.mark.asyncio
async def test_eth_get_balance():
    # Setup mock storage and HTTP handler
    storage = MockStorage()
    request_handler = MockRequestHandler()
    
    # Configure mock responses
    request_handler.add_response(
        "eth_getBalance",
        ["0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5", "latest"],
        "0x1bc16d674ec80000"  # 2 ETH
    )
    
    client = Colibri(
        storage=storage,
        request_handler=request_handler
    )
    
    balance = await client.rpc("eth_getBalance", [
        "0x95222290DD7278Aa3Ddd389Cc1E1d165CC4BAfe5",
        "latest"
    ])
    
    assert balance == "0x1bc16d674ec80000"
```

## Testing

```bash
# Run all tests
python -m pytest tests/

# Run with coverage
python -m pytest tests/ --cov=colibri --cov-report=html
```

## Development

### Building Debug Version

```bash
./build.sh --debug
```

### Running Specific Tests

```bash
python -m pytest tests/test_storage.py -v
python -m pytest tests/test_rpc.py::test_eth_get_balance -v
```

## Supported Methods

The library supports all standard Ethereum JSON-RPC methods that can be proven:

- `eth_getBalance`
- `eth_getCode`  
- `eth_getStorageAt`
- `eth_getTransactionByHash`
- `eth_getTransactionReceipt`
- `eth_getBlockByHash`
- `eth_getBlockByNumber`
- `eth_getLogs`
- `eth_call`
- And many more...

## Chain Configuration

### Supported Chains

| Chain | Chain ID | Alias |
|-------|----------|-------|
| Ethereum Mainnet | 1 | "mainnet", "eth" |
| Sepolia Testnet | 11155111 | "sepolia" |
| Gnosis Chain | 100 | "gnosis", "xdai" |
| Gnosis Chiado | 10200 | "chiado" |

### Custom Chain Configuration

```python
client = Colibri(
    chain_id=42161,  # Arbitrum One
    eth_rpcs=["https://arb1.arbitrum.io/rpc"],
    beacon_apis=["https://arbitrum-beacon-api.example.com"]
)
```

## License

MIT License - see [LICENSE](../../LICENSE) file for details.