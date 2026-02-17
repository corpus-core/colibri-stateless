# src/util/ - Utility Modules

Core utility library used by all other modules. Contains SSZ encoding, byte handling, state machine, cryptography, JSON parsing, and plugin system.

## File Overview

### Core Types and Data Handling

| File | Purpose |
|------|---------|
| `bytes.h` / `bytes.c` | `bytes_t` fat pointer (`{len, data}`) and growable `buffer_t`. Safe memory allocation (`safe_malloc`, `safe_calloc`). Hex encoding/decoding, `bprintf()` formatted buffer writing. |
| `ssz.h` / `ssz.c` | SSZ (Simple Serialize) encoding/decoding. `ssz_def_t` type definitions, `ssz_ob_t` typed object wrapper. Predefined types: `ssz_uint8`, `ssz_uint32`, `ssz_bytes32`, etc. |
| `ssz_merkle.c` | Merkle tree operations: proof generation/verification, generalized indices (`gindex_t`), hash tree root calculation. |
| `ssz_builder.c` | SSZ builder API: construct SSZ objects programmatically, add fields to containers/lists. |
| `json.h` / `json.c` | Zero-copy JSON parser. `json_t` tokens reference the original string. `json_parse()`, `json_get()`, `json_as_string()`. |
| `json_validate.c` | JSON validation: schema checking, property validation, type verification against SSZ types. |

### State Machine and Error Handling

| File | Purpose |
|------|---------|
| `state.h` / `state.c` | Async state machine. `c4_state_t` holds linked list of `data_request_t` and error string. Key macros: `TRY_ASYNC()`, `THROW_ERROR()`, `TRY_ADD_ASYNC()`. |

### Cryptography

| File | Purpose |
|------|---------|
| `crypto.h` / `crypto.c` | Hash functions (keccak, sha256), BLS signature verification, secp256k1, address derivation. Size constants: `HASH_LEN` (32), `ADDRESS_LEN` (20), `BLS_PUBKEY_LEN` (48). |

### Infrastructure

| File | Purpose |
|------|---------|
| `plugin.h` / `plugin.c` | Plugin system: `storage_plugin_t` interface for sync state persistence (file-based default if `FILE_STORAGE`). Parallel-for hooks. |
| `logger.h` / `logger.c` | Logging with color support. Log levels, request info formatting. |
| `witness.h` / `witness.c` | Witness proofs for L2 verification before L1 commitment. BlockHash witness SSZ types. |
| `chains.h` / `chains.c` | Chain ID constants (Mainnet=1, Sepolia=11155111, OP=10, Base=8453, ...) and chain type enum (`C4_CHAIN_TYPE_ETHEREUM`, `C4_CHAIN_TYPE_OPTIMISM`). |
| `version.h` / `version.c` | Version constants and `c4_print_version()`. |
| `common.h` | Visibility attributes, unused markers, compiler macros. |
| `compat.h` | Platform compatibility (inttypes.h for embedded). |

## Key Patterns

### bytes_t Usage

`bytes_t` is a fat pointer passed by value (not by reference). It does not own its memory -- it is always a view into some other buffer.

```c
bytes_t data = {.len = 32, .data = some_buffer};
bytes_t empty = NULL_BYTES;            // {0, NULL}
bool eq = bytes_eq(a, b);             // Compare two byte spans
bytes_t dup = bytes_dup(original);    // Heap-allocate a copy (caller must free)
```

### buffer_t Usage

`buffer_t` is an owned, growable buffer. `allocated > 0` means heap memory (must be freed). `allocated < 0` means fixed/stack buffer (do not free).

```c
buffer_t buf = {0};                   // Start empty
buffer_append(&buf, data);            // Grow and append
bprintf(&buf, "0x%x", value);        // Printf into buffer
bytes_t result = buf.data;            // Read the bytes_t view
buffer_free(&buf);                    // Free if heap-allocated
```

### SSZ Type Definitions

Types are defined declaratively as `ssz_def_t` arrays. The macro system (`SSZ_CONTAINER()`, `SSZ_UINT64()`, `SSZ_BYTES32()`, etc.) makes this concise:

```c
const ssz_def_t MY_TYPE[] = {
    SSZ_CONTAINER("MyType", 3),
    SSZ_UINT64("value"),
    SSZ_BYTES32("hash"),
    SSZ_BOOLEAN("active"),
};
```

Access fields with `ssz_get()`:
```c
ssz_ob_t ob = {.bytes = raw_data, .def = MY_TYPE};
ssz_ob_t hash = ssz_get(&ob, "hash");
uint64_t val = ssz_uint64(ssz_get(&ob, "value"));
```

### c4_state_t and the Async Pattern

`c4_state_t` manages two things: a linked list of `data_request_t` entries (pending external requests) and an error string. It is rarely used standalone -- instead it is embedded as a field in `prover_ctx_t` (`ctx->state`) and `verify_ctx_t` (`ctx->state`).

The host system is responsible for executing data requests and setting their responses (via `c4_req_set_response()` / `c4_req_set_error()`).

`c4_status_t` is the return type for:
- **Async functions** that may need external data: they return `C4_PENDING` when a new `data_request_t` is created, and the host must fetch the data before calling again.
- **Any function that can fail**: `C4_ERROR` indicates an error was added to `state->error`. This is why `TRY_ASYNC()` is used broadly -- not just in async contexts, but wherever error propagation is needed.

```c
// Async function: may return C4_PENDING when external data is needed
c4_status_t my_async_function(c4_state_t* state) {
    data_request_t* req = c4_state_get_pending_request(state);
    if (!req) {
        req = c4_state_add_request(state, ...);
        return C4_PENDING;
    }
    // Process response -- TRY_ASYNC propagates C4_ERROR if validation fails
    TRY_ASYNC(validate_response(state, req));
    return C4_SUCCESS;
}

// Non-async function: uses TRY_ASYNC purely for error propagation
c4_status_t parse_something(c4_state_t* state, bytes_t input) {
    if (input.len == 0)
        THROW_ERROR("input is empty");   // adds error to state, returns C4_ERROR
    TRY_ASYNC(validate_format(state, input));  // propagates any C4_ERROR
    return C4_SUCCESS;
}
```

Typical host-side usage (prover example):
```c
prover_ctx_t* ctx = c4_prover_create("eth_getBalance", params, 1, 0);
while (true) {
    switch (c4_prover_execute(ctx)) {
        case C4_PENDING:
            // Fetch all pending requests from ctx->state
            data_request_t* req;
            while ((req = c4_state_get_pending_request(&ctx->state)))
                fetch_and_set_response(req);
            break;
        case C4_SUCCESS:
            // Proof is ready in ctx->proof
            break;
        case C4_ERROR:
            // Error message in ctx->state.error
            break;
    }
}
c4_prover_free(ctx);
```

<!-- AUTO:UTIL_FILES:START -->

### Public Functions (auto-generated)

| File | Public Functions | Description |
|------|----------------|-------------|

<!-- AUTO:UTIL_FILES:END -->
