# Colibri Stateless - Agent Guide

Colibri Stateless is a high-performance prover/verifier for Ethereum and Layer-2 chains (OP-Stack). The core library is written in C with bindings for JavaScript/TypeScript, Python, Kotlin/Java, and Swift. It enables ultra-light clients that cryptographically verify blockchain data without storing full state -- the verifier only stores sync committee state (~27h rotation).

**License**: MIT (core), PolyForm Noncommercial (server component `src/server/`).

## Architecture Overview

```
                  ┌─────────────────────────────────────────────┐
                  │            Bindings / Host System            │
                  │  (JS/TS, Python, Kotlin, Swift, CLI, HTTP)   │
                  └───────────────┬─────────────┬───────────────┘
                                  │             │
                        ┌─────────▼──┐    ┌─────▼────────┐
                        │   Prover   │    │   Verifier   │
                        │ prover.h   │    │  verify.h    │
                        └─────┬──────┘    └──────┬───────┘
                              │                  │
                  ┌───────────▼──────────────────▼───────────┐
                  │          State Machine (state.h)          │
                  │   C4_PENDING / C4_SUCCESS / C4_ERROR      │
                  └───────────────────┬──────────────────────┘
                                      │
                  ┌───────────────────▼──────────────────────┐
                  │         Chain Modules (Plugin System)     │
                  │   chains/eth/  │  chains/op/              │
                  └───────────────────┬──────────────────────┘
                                      │
                  ┌───────────────────▼──────────────────────┐
                  │              Utilities                    │
                  │  ssz.h  bytes.h  crypto.h  json.h        │
                  └──────────────────────────────────────────┘
```

### Core Concepts

- **Prover**: Collects blockchain data from RPC/Beacon API nodes and creates cryptographic proofs for the validity of RPC responses.
- **Verifier**: Validates proofs using only the current sync committee state (no full node needed). Almost stateless.
- **State Machine**: `c4_state_t` manages pending `data_request_t` entries and error messages. It is embedded in `prover_ctx_t` and `verify_ctx_t` (as `ctx->state`). Functions return `c4_status_t`: `C4_PENDING` when external data is needed, `C4_ERROR` on failure, `C4_SUCCESS` when done. The host system is responsible for fetching data for pending requests and setting responses. `TRY_ASYNC()` is used broadly for error propagation, not just in async contexts.
- **Chain Modules**: Ethereum and OP-Stack are implemented as plugins registered via CMake (`add_verifier()` / `add_prover()`). At build time, CMake generates dispatcher headers (`verifiers.h`, `provers.h`).
- **SSZ**: All proofs and data types use Simple Serialize (SSZ) encoding. Types are defined declaratively in C using `ssz_def_t` arrays.

### Request Lifecycle

1. Host creates a prover or verifier context with RPC method, params, and chain ID.
2. Execute function returns `C4_PENDING` with data requests.
3. Host fetches external data (RPC, Beacon API) and sets responses via `c4_req_set_response()`.
4. Repeat until `C4_SUCCESS` (proof/result ready) or `C4_ERROR`.

## Directory Structure

| Directory | Purpose | Details |
|-----------|---------|---------|
| `src/` | Core C library | See [src/AGENTS.md](src/AGENTS.md) |
| `src/verifier/` | Verification engine | `verify.h` is the main API |
| `src/prover/` | Proof generation engine | `prover.h` is the main API |
| `src/chains/eth/` | Ethereum chain module | See [src/chains/eth/AGENTS.md](src/chains/eth/AGENTS.md) |
| `src/chains/op/` | OP-Stack chain module | See [src/chains/op/AGENTS.md](src/chains/op/AGENTS.md) |
| `src/util/` | Utilities (SSZ, bytes, crypto, state, JSON) | See [src/util/AGENTS.md](src/util/AGENTS.md) |
| `src/server/` | HTTP prover server (libuv/llhttp). Do not block the event loop -- use `REQUEST_WORKER_THREAD` for CPU work | See [src/server/AGENTS.md](src/server/AGENTS.md) |
| `src/cli/` | CLI tools (prover, verifier, ssz) | Three executables |
| `bindings/` | Language bindings | See [bindings/AGENTS.md](bindings/AGENTS.md) |
| `bindings/colibri.h` | Public C API for all bindings | JSON-based status protocol |
| `libs/` | Bundled third-party libraries | blst, evmone, libuv, llhttp, zstd, mcl, etc. |
| `test/` | Tests (Unity framework) | See [test/AGENTS.md](test/AGENTS.md) |
| `scripts/` | Build and doc scripts | `doc/`, `create_test.sh`, coverage, valgrind |
| `installer/` | Platform installers | Linux, macOS, Windows, Homebrew, PPA |
| `.github/workflows/` | CI/CD pipelines | cmake.yml, bindings, release, CodeQL |

<!-- AUTO:DIRECTORY_MAP:START -->
- `bindings/` (5 .c, 19 .h) -- Language Bindings
  - `bindings/docker/` -- Colibri Prover - Docker Image
    - `bindings/docker/valgrind-logs/`
  - `bindings/emscripten/` (1 .c) -- C4 (corpus core colibri client)
    - `bindings/emscripten/cjs/`
    - `bindings/emscripten/rn-web-test/`
    - `bindings/emscripten/scripts/`
    - `bindings/emscripten/src/`
    - `bindings/emscripten/test/`
    - `bindings/emscripten/webpack/`
  - `bindings/kotlin/` (1 .c) -- Kotlin/Java Bindings for Colibri
    - `bindings/kotlin/example/` -- Colibri Android Example App
    - `bindings/kotlin/gradle/`
    - `bindings/kotlin/lib/`
  - `bindings/node-addon/`
  - `bindings/python/` -- Colibri Python Bindings (corpus core colibri client)
    - `bindings/python/examples/`
    - `bindings/python/scripts/`
    - `bindings/python/src/`
    - `bindings/python/tests/`
  - `bindings/swift/` (2 .c, 18 .h) -- Colibri Swift Bindings
    - `bindings/swift/Sources/` (1 .c, 2 .h)
    - `bindings/swift/Tests/`
    - `bindings/swift/ios_package/` (1 .c, 16 .h) -- Colibri Swift Bindings
    - `bindings/swift/test_ios_app/` -- Colibri iOS Test App
- `installer/` -- Build with installer support
  - `installer/assets/`
  - `installer/config/`
  - `installer/copr/` -- Fedora Copr (Community Repository)
  - `installer/homebrew/` -- For local use only (secure, recommended for Metamask):
  - `installer/linux/` -- Install the package
    - `installer/linux/debian/`
    - `installer/linux/rpm/`
  - `installer/macos/` -- Server port
    - `installer/macos/resources/`
    - `installer/macos/scripts/`
  - `installer/ppa/` -- Ubuntu PPA (Personal Package Archive)
  - `installer/scripts/`
    - `installer/scripts/launchd/`
    - `installer/scripts/systemd/`
  - `installer/windows/` -- Using PowerShell
- `libs/` (42 .c, 50 .h)
  - `libs/blst/`
  - `libs/crypto/` (40 .c, 47 .h)
    - `libs/crypto/aes/` (5 .c, 4 .h)
    - `libs/crypto/ed25519-donna/` (10 .c, 15 .h)
  - `libs/curl/` (1 .c, 1 .h)
  - `libs/evmone/` (1 .h)
    - `libs/evmone/compat/`
  - `libs/intx/` (1 .c, 1 .h)
  - `libs/libuv/`
  - `libs/llhttp/`
  - `libs/mcl/`
  - `libs/tommath/`
  - `libs/zstd/`
- `scripts/`
  - `scripts/doc/`
- `src/` (135 .c, 61 .h) -- Core C Library
  - `src/chains/` (96 .c, 39 .h)
    - `src/chains/eth/` (73 .c, 28 .h) -- Ethereum Chain Module
    - `src/chains/op/` (23 .c, 11 .h) -- OP-Stack Chain Module
  - `src/cli/` (3 .c, 1 .h)
  - `src/prover/` (1 .c, 1 .h) -- Prover
  - `src/server/` (21 .c, 5 .h) -- HTTP Prover Server
    - `src/server/io/` (2 .c, 2 .h)
    - `src/server/web_ui/` -- Colibri Server Web Configuration UI
  - `src/util/` (13 .c, 14 .h) -- Utility Modules
  - `src/verifier/` (1 .c, 1 .h)
- `test/` (46 .c, 5 .h) -- Test Suite
  - `test/beanchmarks/`
  - `test/data/`
    - `test/data/eth_blockNumber_electra/`
    - `test/data/eth_call1/`
    - `test/data/eth_call3/`
    - `test/data/eth_call_7702/`
    - `test/data/eth_call_authorization_list/`
    - `test/data/eth_call_electra/`
    - `test/data/eth_getBalance1/`
    - `test/data/eth_getBalance_electra/`
    - `test/data/eth_getBlockByHash1/`
    - `test/data/eth_getBlockByNumber1/`
    - `test/data/eth_getBlockByNumber_electra/`
    - `test/data/eth_getBlockHeader1/`
    - `test/data/eth_getLogs1/`
    - `test/data/eth_getLogs_electra/`
    - `test/data/eth_getProof1/`
    - `test/data/eth_getProof2/`
    - `test/data/eth_getStorageAt1/`
    - `test/data/eth_getStorageAt_electra/`
    - `test/data/eth_getTransactionByBlockHashAndIndex1/`
    - `test/data/eth_getTransactionByHash1/`
    - `test/data/eth_getTransactionByHash2/`
    - `test/data/eth_getTransactionByHash_electra/`
    - `test/data/eth_getTransactionCount1/`
    - `test/data/eth_getTransactionCount_electra/`
    - `test/data/eth_getTransactionReceipt1/`
    - `test/data/eth_getTransaction_Type_4/`
    - `test/data/eth_getTransactionreceipt_electra/`
    - `test/data/eth_sync/`
    - `test/data/log_cache/`
    - `test/data/precompile_identity/`
    - `test/data/precompile_ripemd160/`
    - `test/data/precompile_sha256/`
    - `test/data/server/`
    - `test/data/simulate_simple/`
    - `test/data/simulate_weth/`
    - `test/data/trusted_block1/`
    - `test/data/uv_util_read/`
    - `test/data/uv_util_write/`
    - `test/data/zk_data/`
  - `test/embedded/` (6 .c, 1 .h) -- Build the Docker image
  - `test/eth/`
    - `test/eth/TrieTests/`
  - `test/unittests/` (40 .c, 4 .h)
  - `test/valgrind/` -- Valgrind Suppressions
<!-- AUTO:DIRECTORY_MAP:END -->

## Key Data Structures

| Type | Header | Description |
|------|--------|-------------|
| `bytes_t` | `src/util/bytes.h` | Fat pointer: `{uint32_t len, uint8_t* data}`. Passed by value. |
| `buffer_t` | `src/util/bytes.h` | Growable byte buffer: `{bytes_t data, int32_t allocated}`. Negative `allocated` = fixed/stack buffer. |
| `ssz_ob_t` | `src/util/ssz.h` | SSZ object: `{bytes_t bytes, ssz_def_t* def}`. Typed view over raw bytes. |
| `ssz_def_t` | `src/util/ssz.h` | SSZ type definition. Declarative schema for containers, lists, unions, etc. |
| `c4_state_t` | `src/util/state.h` | Async state: linked list of `data_request_t` + error string. |
| `data_request_t` | `src/util/state.h` | Pending data request: URL, method, payload, response, node selection. |
| `verify_ctx_t` | `src/verifier/verify.h` | Verification context: proof, data, sync_data, state, result. |
| `prover_ctx_t` | `src/prover/prover.h` | Prover context: method, params, chain_id, state, proof output. |
| `c4_status_t` | `src/util/state.h` | Enum: `C4_SUCCESS=0`, `C4_ERROR=-1`, `C4_PENDING=2`. |
| `json_t` | `src/util/json.h` | Parsed JSON token referencing source string. |

## Build System

### Quick Build

```bash
cmake --preset default        # Configure (Debug, with OP-Stack + HTTP server + tests)
cmake --build build/default   # Build
ctest --test-dir build/default  # Run tests
```

### CMake Presets

| Preset | Description |
|--------|-------------|
| `default` | Debug build with CHAIN_OP, HTTP_SERVER, PROVER_CACHE, TEST |
| `testing` | Debug build with TEST + COVERAGE |
| `full-features` | Release build with TEST + HTTP_SERVER |
| `wasm` | MinSizeRel WASM build via Emscripten |
| `wasm-profile` | WASM build for browser profiling |

### Key CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `CHAIN_ETH` | ON | Ethereum verification support |
| `CHAIN_OP` | OFF | OP-Stack verification support |
| `PROVER` | ON | Build prover library |
| `VERIFIER` | ON | Build verifier library |
| `CLI` | ON | Build command line tools |
| `CURL` | ON | Enable CURL for HTTP requests |
| `HTTP_SERVER` | OFF | Build HTTP prover server (libuv/llhttp) |
| `TEST` | OFF | Build unit tests |
| `USE_MCL` | OFF | Enable MCL for ZK proof verification |
| `ETH_ZKPROOF` | ON | ETH ZK-Proof verification support |
| `EMBEDDED` | OFF | Build for embedded target |
| `WASM` | OFF | Build WebAssembly target |
| `STATIC_MEMORY` | OFF | Static memory allocation (embedded) |
| `SHAREDLIB` | OFF | Build shared library |
| `COVERAGE` | OFF | Enable coverage tracking |

<!-- AUTO:CMAKE_OPTIONS:START -->

### All CMake Options (auto-generated)

| Option | Default | Description | Source |
|--------|---------|-------------|--------|
| `BLOCK_HASH_CACHE` | ON | Cache block hashes for faster verification within the same block | CMakeLists.txt |
| `BLS_DESERIALIZE` | ON | Store BLS keys deserialized. It is faster but uses 25k more memory in cache per period. | CMakeLists.txt |
| `BUILD_KONA_BRIDGE` | ON | Build Kona-P2P bridge for native OP-Stack compatibility | src/chains/op/kona_bridge/CMakeLists.txt |
| `C4_PYTHON` | OFF | Build Python bindings | CMakeLists.txt |
| `CHAIN_ETH` | ON | includes the ETH verification support | CMakeLists.txt |
| `CHAIN_OP` | OFF | includes the OP-Stack verification support | CMakeLists.txt |
| `CLI` | ON | Build command line tools | CMakeLists.txt |
| `COMBINED_STATIC_LIB` | OFF | Build a combined static library | CMakeLists.txt |
| `COVERAGE` | OFF | Enable coverage tracking | CMakeLists.txt |
| `CURL` | ON | Enable CURL support | CMakeLists.txt |
| `EMBEDDED` | OFF | Build for embedded target | CMakeLists.txt |
| `EMBEDDED_ASM_M_PROFILE` | OFF | Use Cortex-M profile for assembly files | test/embedded/CMakeLists.txt |
| `ETH_ACCOUNT` | ON | support eth account verification. eth_getBalance, eth_getStorageAt, eth_getProof, eth_getCode, eth_getTransactionCount | src/chains/eth/CMakeLists.txt |
| `ETH_BLOCK` | ON | support eth block verification. eth_getBlockByHash, eth_getBlockByNumber, eth_getBlockTransactionCountByHash, eth_getBlockTransactionCountByNumber, eth_getUncleCountByBlockHash, eth_getUncleCountByBlockNumber | src/chains/eth/CMakeLists.txt |
| `ETH_CALL` | ON | support eth call verification. eth_call, eth_estimateGas | src/chains/eth/CMakeLists.txt |
| `ETH_LOGS` | ON | support eth logs verification. eth_getLogs | src/chains/eth/CMakeLists.txt |
| `ETH_PRECOMPILE_EMBED` | ON | Embed KZG trusted setup G2^tau as a generated header | src/chains/eth/precompiles/CMakeLists.txt |
| `ETH_RECEIPT` | ON | support eth receipt verification. eth_getTransactionReceipt | src/chains/eth/CMakeLists.txt |
| `ETH_TX` | ON | support eth Transaction verification. eth_getTransactionByHash, eth_getTransactionByBlockHashAndIndex, eth_getTransactionByBlockNumberAndIndex | src/chains/eth/CMakeLists.txt |
| `ETH_UTIL` | ON | support eth utils like eth_chainId or web3_sha3 | src/chains/eth/CMakeLists.txt |
| `ETH_ZKPROOF` | ON | includes the ETH ZK-Proof verification support | CMakeLists.txt |
| `ETH_ZKPROOF_BUILD_HOST` | OFF | Build eth-sync-script host binary (Rust/SP1 | CMakeLists.txt |
| `EVMLIGHT` | OFF | uses evmlight vor eth_call verification, which is smaller and faster, but does not track gas. | src/chains/eth/CMakeLists.txt |
| `EVMONE` | ON | uses evmone to verify eth_calls | src/chains/eth/CMakeLists.txt |
| `FILE_STORAGE` | ON | if activated the verfifier will use a simple file-implementaion to store states in the current folder or in a folder specified by the env varC4_STATE_DIR | CMakeLists.txt |
| `GENERATE_JAVA_SOURCES` | OFF | Generate Java sources using SWIG | bindings/kotlin/CMakeLists.txt |
| `HTTP_SERVER` | OFF | Build the HTTP server using libuv and llhttp | CMakeLists.txt |
| `HTTP_SERVER_GEO` | ON | support for geo-location | CMakeLists.txt |
| `INSTALLER` | OFF | Build installer packages (requires HTTP_SERVER=ON | CMakeLists.txt |
| `KOTLIN` | OFF | Build Kotlin bindings | CMakeLists.txt |
| `MESSAGES` | ON | if activated the binaries will contain error messages, but for embedded systems this is not needed and can be turned off to save memory | CMakeLists.txt |
| `PRECOMPILE_ZERO_HASHES` | OFF | Enable precomputed zero hashes cache (1 KB RAM | src/util/CMakeLists.txt |
| `PRECOMPILES_BN128` | OFF | Precompile BN128 (ecadd, ecmul, ecpairing | src/chains/eth/CMakeLists.txt |
| `PRECOMPILES_KZG` | OFF | Precompile KZG (point evaluation | src/chains/eth/CMakeLists.txt |
| `PRECOMPILES_RIPEMD160` | ON | Precompile ripemd160 | src/chains/eth/CMakeLists.txt |
| `PROVER` | ON | Build the prover library | CMakeLists.txt |
| `PROVER_CACHE` | OFF | Caches blockhashes and maps, which makes a lot of sense on a server | CMakeLists.txt |
| `PROVER_TRACE` | OFF | Collect lightweight prover-internal spans for server export | CMakeLists.txt |
| `SHAREDLIB` | OFF | Build shared library | CMakeLists.txt |
| `STATIC_MEMORY` | OFF | if true, the memory will be statically allocated, which only makes sense for embedded systems | CMakeLists.txt |
| `SWIFT` | OFF | Build Swift bindings | CMakeLists.txt |
| `TEST` | OFF | Build the unit tests | CMakeLists.txt |
| `USE_CHECKPOINTZ` | OFF | enable checkpoint fetching from checkpointz (beacon node | src/chains/eth/CMakeLists.txt |
| `USE_MCL` | OFF | Enable MCL support for ZK-Proof verification | CMakeLists.txt |
| `VERIFIER` | ON | Build the verifier library | CMakeLists.txt |
| `WASM` | OFF | Build WebAssembly target | CMakeLists.txt |
| `WASM_DEBUG` | OFF | Enable DWARF debug info for source-level WASM debugging | bindings/emscripten/CMakeLists.txt |
| `WASM_EMBED` | OFF | Embed WASM into JS (SINGLE_FILE=1 | bindings/emscripten/CMakeLists.txt |
| `WASM_PROFILE` | OFF | Enable profiling-friendly WASM build (function names + source maps | bindings/emscripten/CMakeLists.txt |
| `WITNESS_SIGNER` | ON | Enable witness signing | CMakeLists.txt |

<!-- AUTO:CMAKE_OPTIONS:END -->

## Coding Conventions

### Naming

- **Functions**: `module_action()` or `module_action_object()` in `snake_case`. Prefixes: `c4_` (core API), `ssz_` (SSZ), `bytes_`/`buffer_` (bytes), `json_` (JSON).
- **Types**: `snake_case_t` suffix: `bytes_t`, `ssz_ob_t`, `verify_ctx_t`.
- **Enums**: `UPPER_SNAKE_CASE` values: `C4_SUCCESS`, `SSZ_TYPE_UINT`.
- **Macros**: `UPPER_SNAKE_CASE`: `TRY_ASYNC()`, `THROW_ERROR()`, `NULL_BYTES`.
- **Files**: `snake_case.c` / `snake_case.h` pairs in the same directory.

### Memory Management

- Use `safe_malloc()`, `safe_calloc()`, `safe_realloc()` (abort on OOM).
- Use `safe_free()` for deallocation.
- `buffer_t` manages growable buffers. `allocated > 0` means heap-allocated; `allocated < 0` means fixed/stack buffer (do not free).
- Ownership annotations: `M_RET` (returns allocated memory), `M_TAKE(n)` (takes ownership of param n). Used by Clang static analyzer.

### Error Handling

- Functions return `c4_status_t` (`C4_SUCCESS`, `C4_ERROR`, `C4_PENDING`).
- Error messages accumulate in `c4_state_t.error`.
- Key macros:
  - `TRY_ASYNC(fn)` -- return early if fn is not `C4_SUCCESS`.
  - `THROW_ERROR(msg)` -- add error to state and return `C4_ERROR`.
  - `RETURN_VERIFY_ERROR(msg)` -- verification-specific error return.

### Headers

- Include guards: `#ifndef filename_h__` / `#define filename_h__` (not `#pragma once`).
- All headers wrap content with `#ifdef __cplusplus extern "C" { #endif`.
- Includes: local headers first (`"./header.h"`), then system headers (`<stdlib.h>`).

### Function Annotations

- `NONNULL` / `NONNULL_FOR((n))` -- mark non-null parameters.
- `RETURNS_NONNULL` -- function never returns NULL.
- `COUNTED_BY(len)` -- array size annotation for bounds checking.

### Comments

- All comments must be in English.
- Documentation for public API: `/** ... */` with `@param` and `@return` tags, using Markdown syntax.
- Only `@param` and `@return` are allowed as documentation tags.
- Section markers for doc generation: `// :` (top-level), `// ::` (subsection), `// :::` (detail).

## Testing

- **Framework**: Unity (v2.5.2), auto-fetched by CMake.
- **Test files**: `test/unittests/test_*.c` -- auto-discovered by CMake.
- **Pattern**: `setUp()`, `tearDown()`, `test_*()` functions, `UNITY_BEGIN()`/`UNITY_END()` in `main()`.
- **Test data**: `test/data/<testname>/` directories with `test.json` and state files.
- **Create new tests**: `./scripts/create_test.sh <testname> <rpc_method> <args...>` generates test data.
- **Run**: `ctest --test-dir build/default` or `./build/default/test/unittests/test_<name>`.

## Documentation Generation

The gitbook documentation is generated from code comments using `scripts/doc/index.js`. The same `// :` / `// ::` / `// :::` comment syntax in source files drives both the gitbook docs and parts of this agent documentation.

Auto-generated sections in AGENTS.md files are marked with `<!-- AUTO:...:START -->` / `<!-- AUTO:...:END -->` comments and updated by running:

```bash
node scripts/doc/agents.js
```

<!-- AUTO:MODULE_INDEX:START -->

### Public API Index (auto-generated)

<!-- AUTO:MODULE_INDEX:END -->
