# bindings/ - Language Bindings

All bindings wrap the C API defined in `colibri.h` (this directory) and follow a common architecture pattern.

## Common Architecture

All bindings implement the same lifecycle:

1. **Create context**: Call `c4_create_prover_ctx()` or `c4_verify_create_ctx()` with method, params, and chain ID.
2. **Execute loop**: Call `c4_prover_execute_json_status()` / `c4_verify_execute_json_status()` repeatedly.
3. **Handle pending requests**: When status is `"pending"`, fetch data from network for each request.
4. **Set responses**: Call `c4_req_set_response()` or `c4_req_set_error()` for each request.
5. **Get result**: When status is `"success"`, extract the result.
6. **Cleanup**: Free the context.

The JSON status protocol (returned by `*_execute_json_status()`) uses this format:
```json
{"status": "pending", "requests": [{"url": "...", "method": "POST", "body": "..."}]}
{"status": "success", "result": "0x..."}
{"status": "error", "error": "description"}
```

## Public C API (`colibri.h`)

| Function | Purpose |
|----------|---------|
| `c4_create_prover_ctx()` | Create prover context |
| `c4_prover_execute_json_status()` | Execute prover, get JSON status |
| `c4_prover_get_proof()` | Get generated proof bytes |
| `c4_free_prover_ctx()` | Free prover context |
| `c4_verify_create_ctx()` | Create verifier context |
| `c4_verify_execute_json_status()` | Execute verifier, get JSON status |
| `c4_verify_free_ctx()` | Free verifier context |
| `c4_req_set_response()` | Set response data for a pending request |
| `c4_req_set_error()` | Set error for a pending request |

## Binding Overview

| Binding | Directory | Language | Build System | Package |
|---------|-----------|----------|-------------|---------|
| Emscripten | `emscripten/` | TypeScript/JS | CMake + Emscripten + Webpack | `@corpus-core/colibri-stateless` (npm) |
| Python | `python/` | Python | CMake + setuptools | `colibri-stateless` (PyPI) |
| Kotlin | `kotlin/` | Kotlin/Java | CMake + Gradle | `com.corpuscore:colibri-jar` / `colibri-aar` |
| Swift | `swift/` | Swift | CMake + SwiftPM | Swift Package |
| Docker | `docker/` | -- | Docker | `ghcr.io/corpus-core/colibri-prover` |

## Emscripten (JavaScript/TypeScript)

**Key files:**
- `src/index.ts` -- Main `C4Client` class, high-level API
- `src/wasm.ts` -- WASM module loading, C API wrappers
- `src/types.ts` -- TypeScript type definitions
- `src/http.ts` -- HTTP request handling (fetch-based)
- `src/chains.ts` -- Chain configuration
- `src/strategy.ts` -- Request strategy (prover vs direct RPC)
- `src/subscriptionManager.ts` -- Ethereum subscription support (`eth_subscribe`)
- `src/transactionVerifier.ts` -- Transaction verification utilities

**Build**: WASM binary built via Emscripten CMake toolchain, wrapped by TypeScript.

## Python

**Key files:**
- `src/colibri/client.py` -- `ColibriClient` class
- `src/colibri/types.py` -- Python type definitions
- `src/colibri/storage.py` -- Storage backends (file, in-memory)
- `src/colibri/testing.py` -- Testing utilities

**Build**: C extension via CMake, packaged with `setup.py` / `pyproject.toml`.

## Kotlin/Java

**Key files:**
- `lib/src/main/kotlin/com/corpuscore/colibri/Colibri.kt` -- Main `Colibri` class
- `lib/src/main/kotlin/com/corpuscore/colibri/NativeLoader.kt` -- Native library loading (JVM + Android)

**Build**: JNI wrapper, built via CMake + Gradle. Produces JAR (JVM) and AAR (Android).

## Swift

**Key files:**
- `Sources/Colibri/Colibri.swift` -- Main `Colibri` class
- `Sources/CColibri/` -- C bridge headers

**Build**: CMake builds static library, Swift Package Manager wraps it.

## Docker

**Key files:**
- `Dockerfile` -- Multi-stage build for prover server
- `docker-compose.yml` -- Production setup with Memcached
- `docker-compose.valgrind.yml` -- Valgrind memory testing

**Image**: `ghcr.io/corpus-core/colibri-prover:latest` (linux/amd64, linux/arm64).

<!-- AUTO:BINDINGS_INDEX:START -->

### Binding Modules (auto-generated)

- `dart/` -- 347 files
- `docker/` -- 7 files
- `emscripten/` -- 95 files
- `kotlin/` -- 36 files
- `python/` -- 27 files
- `swift/` -- 14 files

<!-- AUTO:BINDINGS_INDEX:END -->
