# test/ - Test Suite

## Framework

Tests use the [Unity](https://github.com/ThrowTheSwitch/Unity) framework (v2.5.2), auto-fetched by CMake via `FetchContent`.

## Structure

| Directory | Purpose |
|-----------|---------|
| `unittests/` | Unit test source files (`test_*.c`) -- auto-discovered by CMake |
| `data/` | Test data (JSON fixtures, SSZ test data, recorded responses) |
| `embedded/` | Embedded system tests (ARM Cortex-A15) |
| `valgrind/` | Valgrind memory testing configuration |

## Test File Pattern

Every test file follows this structure:

```c
#include "unity.h"

void setUp(void) {
    // Called before each test (optional)
}

void tearDown(void) {
    // Called after each test (optional)
}

void test_feature_description(void) {
    // Test assertions:
    // TEST_ASSERT_EQUAL_INT(expected, actual);
    // TEST_ASSERT_EQUAL_STRING(expected, actual);
    // TEST_ASSERT_EQUAL_MEMORY(expected, actual, len);
    // TEST_ASSERT_TRUE(condition);
    // TEST_ASSERT_NULL(ptr);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_feature_description);
    return UNITY_END();
}
```

## Test Categories

### Core / Utility Tests
- `test_core.c` -- Core functionality
- `test_util_plugin.c` -- Plugin system
- `test_util_version.c` -- Version system

### Ethereum Verification Tests
- `test_eth_verify_tx.c` -- Transaction verification
- `test_eth_verify_block.c` -- Block verification
- `test_eth_verify_receipt.c` -- Receipt verification
- `test_eth_verify_logs.c` -- Log verification
- `test_eth_verify_call.c` -- `eth_call` verification
- `test_eth_verify_storage_at.c` -- Storage verification
- `test_eth_verify_accout.c` -- Account verification
- `test_eth_verify_simulate.c` -- Transaction simulation
- `test_eth_sync.c` -- Sync committee operations
- `test_eth_local.c` -- Local Ethereum methods
- `test_eth_ssz.c` -- Ethereum SSZ encoding
- `test_eth_ssz_merkle.c` -- SSZ Merkle trees
- `test_eth_patricia_trie.c` -- Patricia trie operations
- `test_eth_precompiles.c` -- EVM precompiles

### Server Tests (require `HTTP_SERVER=ON`)
- `test_server.c` -- Main server functionality
- `test_server_configure.c` -- Server configuration
- `test_server_config_api.c` -- Configuration API
- `test_server_select.c` -- Node selection
- `test_server_headers.c` -- HTTP header parsing
- `test_server_period_store.c` -- Period storage
- `test_server_period_backfill.c` -- Period backfill
- `test_server_lcu.c` -- Light client updates
- `test_http_client_errors.c` -- HTTP client errors
- `test_beacon_watcher.c` -- Beacon chain watcher

### Dependency Tests
- `test_dependency_blst.c` -- BLS cryptography (BLST)
- `test_dependency_ecdsa.c` -- ECDSA signatures
- `test_dependency_bignum.c` -- Big number arithmetic
- `test_dependency_sha.c` -- SHA hashing
- `test_dependency_memzero.c` -- Memory zeroing

### Other Tests
- `test_bn254.c` -- BN254 curve operations
- `test_zk_proof.c` -- Zero-knowledge proofs
- `test_transaction_cache.c` -- Transaction cache
- `test_logs_cache.c` -- Logs cache

## Test Data

Test data lives in `test/data/`. Tests use a file mock helper (`file_mock_helper.h`) to replace HTTP requests with file URLs pointing to recorded responses. Test data directories typically contain:
- `test.json` -- RPC request, args, and expected result
- State/response files -- recorded HTTP responses from RPC/Beacon nodes

## Creating New Tests

### Automated (for RPC verification tests)

```bash
./scripts/create_test.sh <testname> <rpc_method> <args...>
# Example:
./scripts/create_test.sh eth_getBlock1 eth_getBlockByNumber latest false
```

This generates test data in `test/data/<testname>/` and verifies the proof.

### Manual

1. Create `test/unittests/test_<feature>.c` following the Unity pattern above.
2. The file is auto-discovered by CMake (no `CMakeLists.txt` changes needed).
3. Build and run:
   ```bash
   cmake --build build/default
   ./build/default/test/unittests/test_<feature>
   ```

## Running Tests

```bash
# All tests
ctest --test-dir build/default

# Specific test
./build/default/test/unittests/test_core

# With coverage
cmake --preset testing
cmake --build build/testing
./scripts/run_coverage.sh

# With Valgrind
./scripts/run_valgrind.sh
```

## Embedded Tests

Located in `test/embedded/`. Targets ARM Cortex-A15 for minimal memory footprint testing.

- `minimal_verify.c` -- Minimal memory verification test
- `verify_embedded.c` -- Full embedded verification
- `toolchain.cmake` -- ARM cross-compilation toolchain
- Memory targets: ~149 KB Flash, ~107 KB RAM

Build with `EMBEDDED=ON` and `STATIC_MEMORY=ON`.
