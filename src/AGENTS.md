# src/ - Core C Library

This directory contains the core C implementation of Colibri Stateless. All modules are built into a static library via CMake.

## Module Overview

| Module | Directory | Description |
|--------|-----------|-------------|
| Verifier | `verifier/` | Proof verification engine. Entry point: `verify.h` |
| Prover | `prover/` | Proof generation engine. Entry point: `prover.h` |
| Ethereum | `chains/eth/` | ETH chain module (verification, proofs, SSZ types). See [chains/eth/AGENTS.md](chains/eth/AGENTS.md) |
| OP-Stack | `chains/op/` | OP-Stack chain module (preconfs, ZSTD). See [chains/op/AGENTS.md](chains/op/AGENTS.md) |
| Utilities | `util/` | SSZ, bytes, state machine, crypto, JSON, logging. See [util/AGENTS.md](util/AGENTS.md) |
| Server | `server/` | HTTP prover server (libuv/llhttp). See [server/AGENTS.md](server/AGENTS.md) |
| CLI | `cli/` | Command-line tools (prover, verifier, ssz) |

## Dependency Graph

```
  cli/          server/          bindings/colibri.h
   │               │                    │
   ▼               ▼                    ▼
  prover/ ◄──────────────────────► verifier/
   │                                    │
   ▼                                    ▼
  chains/eth/    chains/op/        (chain modules)
   │                │
   ▼                ▼
  util/  (ssz, bytes, state, crypto, json)
   │
   ▼
  libs/  (blst, evmone, crypto, libuv, llhttp, zstd)
```

Both prover and verifier depend on the chain modules, which register themselves via CMake (`add_verifier()` / `add_prover()`). At build time, CMake generates dispatcher headers in the build directory:
- `verifiers.h` -- dispatches verification to the correct chain module.
- `provers.h` -- dispatches proof generation to the correct chain module.

## Chain Module Registration

Chain modules are registered in their respective `CMakeLists.txt` using CMake functions defined in `chains/chains.cmake`:

```cmake
add_verifier(
  NAME eth_verifier
  GET_REQ_TYPE c4_eth_get_request_type
  VERIFY c4_eth_verify
  METHOD_TYPE c4_eth_get_method_type
)

add_prover(
  NAME eth_prover
  PROOF eth_prover_execute
)
```

The generated headers collect all registered modules and create dispatcher functions that route requests by chain type.

## CLI Tools

| Tool | Source | Purpose |
|------|--------|---------|
| `colibri-prover` | `cli/prover.c` | Generate proofs: `colibri-prover -o proof.ssz eth_getBlockByNumber latest false` |
| `colibri-verifier` | `cli/verifier.c` | Verify proofs: `colibri-verifier -s sync.ssz proof.ssz` |
| `colibri-ssz` | `cli/ssz.c` | Convert SSZ to JSON: `colibri-ssz -t signedblock proof.ssz` |

All tools read `c4_config.json` (or `C4_CONFIG` env var) for RPC/Beacon API endpoint configuration. Support `-c <chain_id>` for chain selection.

<!-- AUTO:SRC_MODULE_INDEX:START -->

### Source Modules (auto-generated)

- `chains/` -- 109 .c, 46 .h files
- `cli/` -- 3 .c, 1 .h files
- `prover/` -- 1 .c, 1 .h files
- `server/` -- 21 .c, 5 .h files
- `util/` -- 14 .c, 15 .h files
- `verifier/` -- 1 .c, 1 .h files

<!-- AUTO:SRC_MODULE_INDEX:END -->
