# src/server/ - HTTP Prover Server

Single-threaded HTTP server using libuv (async I/O) and llhttp (HTTP parsing). Generates cryptographic proofs on request. Licensed under PolyForm Noncommercial (separate from MIT-licensed core).

## Architecture

```
  Client HTTP Request
         │
         ▼
  llhttp (parser) ──► Route Dispatch
                          │
         ┌────────────────┼────────────────┐
         ▼                ▼                ▼
    /proof            /rpc             /health
    (worker thread)   (worker thread)   (main thread)
         │                │
         ▼                ▼
    Prover Engine    Prover + Verifier
         │                │
         ▼                ▼
    SSZ Response     JSON Response
```

Almost all I/O runs on the libuv `default_loop` in the main thread. **It is critical not to block this event loop.** When proof generation involves CPU-intensive work, the prover must request a worker thread using `REQUEST_WORKER_THREAD(ctx)` (defined in `prover.h`). Before switching to a worker thread, all values from the global cache must be read via `c4_prover_cache_get()` so they are local to `prover_ctx_t` -- cache access from worker threads is restricted to prevent race conditions.

### Threading Model

```
Main Thread (libuv default_loop)
  ├── HTTP parsing (llhttp)
  ├── Route dispatch
  ├── External HTTP requests (http_client.c)
  ├── Internal calls (C4_DATA_TYPE_INTERN)
  └── Light handlers (/health, /metrics)

Worker Threads (libuv work queue)
  └── CPU-intensive proof generation
      (triggered by REQUEST_WORKER_THREAD)
```

### Route Registration

HTTP routes are registered with `c4_register_http_handler(handler)`. The handler function (`http_handler`, signature: `bool (*)(client_t*)`) inspects the incoming request and returns `true` if it handles the request (and executes it), or `false` to pass it to the next handler.

### Data Request Types

Each `data_request_t` has a `type` that determines which node list it uses:

| Type | Description |
|------|-------------|
| `C4_DATA_TYPE_BEACON_API` | Beacon API nodes |
| `C4_DATA_TYPE_ETH_RPC` | Ethereum RPC nodes |
| `C4_DATA_TYPE_REST_API` | Generic REST API |
| `C4_DATA_TYPE_INTERN` | Internal async call within the server |
| `C4_DATA_TYPE_PROVER` | Prover server |
| `C4_DATA_TYPE_CHECKPOINTZ` | Checkpointz server |

### Internal Calls (`C4_DATA_TYPE_INTERN`)

For asynchronous operations within the server (e.g., one proof needing results from another internal process), internal call handlers are registered with `c4_register_internal_handler(handler)`. The handler receives the request, performs the work asynchronously, and calls `c4_internal_call_finish(r)` when done to resume the prover execution.

### Cache (Server-Side)

The prover cache (`PROVER_CACHE=ON`) has two levels:
- **Global cache**: Shared across requests, thread-safe reads only from main thread. Use `c4_prover_cache_get()`.
- **Local cache**: Per `prover_ctx_t`, safe in worker threads. Use `c4_prover_cache_get_local()`.
- On context destruction, local entries with `duration_ms > 0` are moved to the global cache.

## Key Files

| File | Purpose |
|------|---------|
| `main.c` | Entry point, server initialization |
| `server.h` | Core server structures and configuration |
| `http_server.c` | HTTP server (libuv TCP + llhttp parsing) |
| `http_client.c` | HTTP client for external RPC/Beacon API requests |
| `http_servers_select.c` | Server selection and load balancing (AIMD algorithm) |
| `http_head_poller.c` | RPC head polling for node availability estimation |
| `handle_proof.c` | `POST /proof` -- proof generation endpoint |
| `handle_verify.c` | `POST /verify` -- proof verification endpoint |
| `handle_unverified_rpc.c` | `POST /rpc` -- RPC passthrough with optional verification |
| `handle_health.c` | `GET /health` -- health check with statistics |
| `handle_metrics.c` | `GET /metrics` -- Prometheus metrics export |
| `handle_openapi.c` | `GET /openapi.yaml` -- OpenAPI 3.1.0 spec |
| `handle_config.c` | Configuration management (requires `WEB_UI_ENABLED`) |
| `cache.c` | Internal proof/request caching |

### Subdirectories

| Directory | Purpose |
|-----------|---------|
| `io/` | libuv utilities: `uv_util.c/h` (async helpers), `configure.c/h` (config parsing) |
| `web_ui/` | Web UI for configuration (`config.html`) |

## API Endpoints

| Method | Path | Description |
|--------|------|-------------|
| POST | `/proof` | Generate SSZ-encoded proof for a JSON-RPC request |
| POST | `/rpc` | Execute JSON-RPC with automatic proof verification |
| POST | `/verify` | Verify existing proof, return result |
| GET | `/health` | Health check with request counts and uptime |
| GET | `/metrics` | Prometheus-format metrics |
| GET | `/openapi.yaml` | OpenAPI specification |
| GET | `/eth/v1/beacon/headers/{block_id}` | Beacon block headers |
| GET | `/eth/v1/beacon/light_client/bootstrap/{root}` | Light client bootstrap |
| GET | `/eth/v1/beacon/light_client/updates` | Sync committee updates |

Full API reference: [API.md](API.md) and [openapi.yaml](openapi.yaml).

## Features

- **Memcached**: Optional caching layer (24h TTL) for external RPC responses. Configured via environment variables.
- **Period Store**: Stores historical block roots (8192 slots per period) for efficient historical proof generation.
- **Beacon Event Streaming**: Subscribes to beacon chain events for proactive cache warming.
- **Load Balancing**: AIMD (Additive Increase Multiplicative Decrease) algorithm for selecting healthy RPC nodes.
- **Head Polling**: Periodically polls RPC nodes to estimate latest block availability.

## Build

```bash
cmake --preset default  # HTTP_SERVER is ON in default preset
cmake --build build/default
./build/default/bin/colibri-server
```

Required CMake option: `HTTP_SERVER=ON`. Also enables `PROVER_CACHE=ON` by default.

<!-- AUTO:SERVER_FILES:START -->

### Public Functions (auto-generated)

| File | Public Functions | Description |
|------|----------------|-------------|

<!-- AUTO:SERVER_FILES:END -->
