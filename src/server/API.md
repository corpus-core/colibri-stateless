: APIs

:: REST API

The Colibri Stateless server provides a comprehensive REST API for generating and verifying cryptographic proofs for blockchain data. This API enables lightweight, stateless verification of Ethereum and Layer-2 blockchain operations.

## Overview

The API consists of the following main categories:

* **Proof Generation** - Create cryptographic proofs for blockchain data
* **Proof Verification** - Verify proofs and execute JSON-RPC requests
* **Beacon Chain API** - Light client sync endpoints for Ethereum consensus layer
* **Health & Monitoring** - Server health checks and Prometheus metrics
* **Configuration** - Server configuration management (requires WEB_UI_ENABLED)

## OpenAPI Specification

The complete API is documented using OpenAPI 3.1.0 specification. You can access the specification in the following ways:

### Live Server Endpoint

If you have a running Colibri server instance, the OpenAPI specification is available at:

```
http://localhost:8090/openapi.yaml
```

Replace `localhost:8090` with your server's host and port.

### Static Specification

The specification is also available in the source repository at:

```
src/server/openapi.yaml
```

## Interactive API Documentation

The OpenAPI specification below provides interactive documentation for all available endpoints.

**Note**: First, add the OpenAPI specification to GitBook via the OpenAPI panel or CLI, then replace `colibri-api` below with your spec name.

### Endpoints

{% openapi-operation spec="colibri-api" path="/rpc" method="post" %}
[OpenAPI colibri-api](https://raw.githubusercontent.com/corpus-core/colibri-stateless/refs/heads/dev/src/server/openapi.yaml)
{% endopenapi-operation %}

{% openapi-operation spec="colibri-api" path="/proof" method="post" %}
[OpenAPI colibri-api](https://raw.githubusercontent.com/corpus-core/colibri-stateless/refs/heads/dev/src/server/openapi.yaml)
{% endopenapi-operation %}

{% openapi-operation spec="colibri-api" path="/config.html" method="get" %}
[OpenAPI colibri-api](https://raw.githubusercontent.com/corpus-core/colibri-stateless/refs/heads/dev/src/server/openapi.yaml)
{% endopenapi-operation %}

{% openapi-operation spec="colibri-api" path="/config" method="get" %}
[OpenAPI colibri-api](https://raw.githubusercontent.com/corpus-core/colibri-stateless/refs/heads/dev/src/server/openapi.yaml)
{% endopenapi-operation %}

{% openapi-operation spec="colibri-api" path="/config" method="post" %}
[OpenAPI colibri-api](https://raw.githubusercontent.com/corpus-core/colibri-stateless/refs/heads/dev/src/server/openapi.yaml)
{% endopenapi-operation %}

{% openapi-operation spec="colibri-api" path="/health" method="get" %}
[OpenAPI colibri-api](https://raw.githubusercontent.com/corpus-core/colibri-stateless/refs/heads/dev/src/server/openapi.yaml)
{% endopenapi-operation %}

{% openapi-operation spec="colibri-api" path="/metrics" method="get" %}
[OpenAPI colibri-api](https://raw.githubusercontent.com/corpus-core/colibri-stateless/refs/heads/dev/src/server/openapi.yaml)
{% endopenapi-operation %}

{% openapi-operation spec="colibri-api" path="/eth/v1/beacon/headers/{block_id}" method="get" %}
[OpenAPI colibri-api](https://raw.githubusercontent.com/corpus-core/colibri-stateless/refs/heads/dev/src/server/openapi.yaml)
{% endopenapi-operation %}

{% openapi-operation spec="colibri-api" path="/eth/v1/beacon/light_client/bootstrap/{block_root}" method="get" %}
[OpenAPI colibri-api](https://raw.githubusercontent.com/corpus-core/colibri-stateless/refs/heads/dev/src/server/openapi.yaml)
{% endopenapi-operation %}

{% openapi-operation spec="colibri-api" path="/eth/v1/beacon/light_client/updates" method="get" %}
[OpenAPI colibri-api](https://raw.githubusercontent.com/corpus-core/colibri-stateless/refs/heads/dev/src/server/openapi.yaml)
{% endopenapi-operation %}

{% openapi-operation spec="colibri-api" path="/openapi.yaml" method="get" %}
[OpenAPI colibri-api](https://raw.githubusercontent.com/corpus-core/colibri-stateless/refs/heads/dev/src/server/openapi.yaml)
{% endopenapi-operation %}

### Schemas

{% openapi-schemas spec="colibri-api" schemas="ProofRequest,JsonRpcRequest,JsonRpcResponse,JsonRpcErrorResponse,ErrorResponse,HealthResponse,ConfigResponse,ConfigParameter,ConfigUpdateRequest,ConfigUpdateResponse,BeaconErrorResponse" grouped="true" %}
[OpenAPI colibri-api](https://raw.githubusercontent.com/corpus-core/colibri-stateless/refs/heads/dev/src/server/openapi.yaml)
{% endopenapi-schemas %}


## Quick Start Examples

### Generate a Proof

Generate a cryptographic proof for an `eth_getBalance` request:

```bash
curl -X POST http://localhost:8090/proof \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "eth_getBalance",
    "params": ["0x742d35Cc6634C0532925a3b844Bc9e7595f0bEb", "latest"],
    "id": 1
  }' \
  --output proof.ssz
```

The response is an SSZ-encoded `C4Request` container. For details on the proof format, see the [Ethereum Main Proof Request](https://corpus-core.gitbook.io/specification-colibri-stateless/specifications/ethereum/ethereum-main-proof-request) specification.

### Verify and Execute a Request

Execute a JSON-RPC request with automatic proof verification:

```bash
curl -X POST http://localhost:8090/rpc \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "eth_getBalance",
    "params": ["0x742d35Cc6634C0532925a3b844Bc9e7595f0bEb", "latest"],
    "id": 1
  }'
```

Response:
```json
{
  "jsonrpc": "2.0",
  "result": "0x0000000000000000000000000000000000000000000000000de0b6b3a7640000",
  "id": 1
}
```

### Execute a Smart Contract Call with Proof

Generate a proof for an `eth_call` request with bytecode included:

```bash
curl -X POST http://localhost:8090/proof \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "eth_call",
    "params": [{
      "to": "0x742d35Cc6634C0532925a3b844Bc9e7595f0bEb",
      "data": "0x06fdde03"
    }, "latest"],
    "id": 1,
    "include_code": true
  }' \
  --output call_proof.ssz
```

The `include_code` property ensures that the contract bytecode is included in the proof, enabling fully stateless verification.

### Update Configuration

Update server configuration programmatically (requires `WEB_UI_ENABLED=1` and a config file):

```bash
curl -X POST http://localhost:8090/config \
  -H "Content-Type: application/json" \
  -d '{
    "parameters": [
      {"env": "PORT", "value": 8091},
      {"env": "RPC_NODES", "value": "https://eth-mainnet.g.alchemy.com/v2/YOUR_KEY"}
    ]
  }'
```

Response:
```json
{
  "success": true,
  "restart_required": true,
  "message": "Configuration saved. Restart server to apply changes.",
  "updated_count": 2
}
```

**Note:** After a successful configuration update, the server automatically restarts (via `exit(0)`), relying on the service manager (systemd/launchd/docker-compose) to restart it with the new settings. This is automatically configured by the [installer packages](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/installer).

### Health Check

Check the server's operational status:

```bash
curl http://localhost:8090/health
```

Response:
```json
{
  "status": "ok",
  "stats": {
    "total_requests": 1523,
    "total_errors": 12,
    "last_sync_event": 125705,
    "last_request_time": 1698234567,
    "open_requests": 3
  }
}
```

### Prometheus Metrics

Retrieve metrics for monitoring:

```bash
curl http://localhost:8090/metrics
```

## Advanced Features

### Client State Parameter

For historic block proofs, you can provide the client's state to optimize proof generation:

```json
{
  "jsonrpc": "2.0",
  "method": "eth_getBalance",
  "params": ["0x742d35Cc6634C0532925a3b844Bc9e7595f0bEb", "0x100000"],
  "id": 1,
  "c4": "0x1234567890abcdef..."
}
```

The `c4` parameter is a hex-encoded representation of the client's current sync committee state, allowing the prover to determine which header proofs are necessary.

### Supported JSON-RPC Methods

The following Ethereum JSON-RPC methods are supported with proof generation:

**Account & Storage:**
- `eth_getBalance`
- `eth_getCode`
- `eth_getStorageAt`
- `eth_getProof`

**Transactions:**
- `eth_getTransactionByHash`
- `eth_getTransactionReceipt`
- `eth_getTransactionCount`

**Blocks:**
- `eth_getBlockByNumber`
- `eth_getBlockByHash`
- `eth_blockNumber`

**Contract Calls:**
- `eth_call`

**Events & Logs:**
- `eth_getLogs`

**Colibri-Specific:**
- `colibri_simulateTransaction` - Simulate transaction execution with proof

For a complete list of supported methods, see the [Supported RPC Methods](https://corpus-core.gitbook.io/specification-colibri-stateless/specifications/ethereum/supported-rpc-methods) documentation.

## Beacon Chain API

The server also provides light client endpoints from the Ethereum Beacon Chain API for sync committee updates:

- `GET /eth/v1/beacon/headers/{block_id}` - Retrieve beacon block headers
- `GET /eth/v1/beacon/light_client/bootstrap/{block_root}` - Initialize light client
- `GET /eth/v1/beacon/light_client/updates` - Sync committee period updates

These endpoints are used internally by the verifier to maintain the current sync committee state and are also available for external light client implementations.

## Error Handling

### HTTP Status Codes

- **200 OK** - Request successful
- **400 Bad Request** - Invalid request format or parameters
- **403 Forbidden** - Web UI disabled (for `/config` and `/config.html`)
- **404 Not Found** - Endpoint or resource not found
- **500 Internal Server Error** - Server error during processing

### Error Response Format

Error responses use the following JSON format:

```json
{
  "error": "Error message describing what went wrong"
}
```

For JSON-RPC errors, the standard JSON-RPC error format is used:

```json
{
  "jsonrpc": "2.0",
  "error": {
    "code": -32000,
    "message": "execution reverted"
  },
  "id": 1
}
```

## Rate Limiting & Performance

The server is designed for high-performance proof generation and verification. Key performance characteristics:

- **Concurrent Requests**: Multiple requests can be processed in parallel
- **Proof Generation**: Typically 100-500ms depending on proof complexity
- **Proof Verification**: Typically 50-200ms depending on proof size
- **Light Client Sync**: Sync committee updates every ~27 hours

For production deployments, consider:
- Using memcached for caching frequently requested proofs (configure via `MEMCACHED_HOST` and `MEMCACHED_PORT`)
- Monitoring via Prometheus metrics at `/metrics`
- Load balancing across multiple RPC nodes (configure via `RPC_NODES`)

## Security Considerations

### Web UI Access

The configuration endpoints (`/config` and `/config.html`) are **disabled by default** for security reasons. Only enable them on trusted networks using:

```bash
# Environment variable
export WEB_UI_ENABLED=1

# Command line flag
./colibri-server -u
```

### Network Binding

By default, the server binds to `127.0.0.1` (localhost only). To expose the server to other machines:

```bash
export HOST=0.0.0.0
export PORT=8090
```

**Warning**: Only expose the server to untrusted networks if you have proper security measures in place (firewall, authentication proxy, etc.).

## Additional Resources

- [Full Specification Documentation](https://corpus-core.gitbook.io/specification-colibri-stateless/)
- [Ethereum Main Proof Request Format](https://corpus-core.gitbook.io/specification-colibri-stateless/specifications/ethereum/ethereum-main-proof-request)
- [Supported RPC Methods](https://corpus-core.gitbook.io/specification-colibri-stateless/specifications/ethereum/supported-rpc-methods)
- [Building & Installation Guide](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/building)
- [GitHub Repository](https://github.com/corpus-core/colibri-stateless)

