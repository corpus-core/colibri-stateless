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
| Host API | `api/` | Public C FFI (`colibri.h`) and unified RPC context (`colibri_common.h`) |

## Dependency Graph

```
  cli/          server/          api/colibri.h
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

## Software quality — documentation

When changing or adding code under `src/`, follow these documentation rules:

- **Standardized function comments** — Every function (public API and internal helpers) must have a consistent `/** ... */` block: brief purpose, then `@param` for each parameter. Use `@return` when the function returns a meaningful value or status (`c4_status_t`, pointers, booleans, etc.). For `void` functions, **omit `@return`** (do not write `@return none`).
- **Explain arguments and return values** — Do not restate the C type alone; say what the value means, valid ranges, ownership (`M_RET` / caller-frees), and error paths when non-obvious.
- **Header vs. implementation** — Document **public functions once**: place the full `/** ... */` block **immediately above the prototype in the `.h` file**. In the matching `.c` file, **do not repeat** that API comment on the definition; use only brief `//` notes for non-obvious implementation details if needed. Functions **static** to a `.c` file (or otherwise not declared in a header) are documented **above the definition in that `.c` file** only.
- **Specification-aligned comments are protected** — Many existing comments also document protocol or product specification (GitBook), including section markers `// :`, `// ::`, `// :::` and prose tied to the Colibri spec. **Do not change that wording** (rephrase, delete, or “fix” spec claims) **without explicit prior agreement** on the exact text to change. Implementation-only edits belong in separate comments or in `@param`/`@return` lines that describe code behavior, not spec policy.
- **Comments are additive only** — You may extend missing or thin documentation. **Never remove factual information** from existing comments (no shortening, “cleanup”, or replacing a detailed `//` / bullet list with a shorter summary unless **every** former fact is preserved verbatim in the same comment block). Reformatting (`//` → `///<`, adding `@param`) is allowed only when the new text is a **superset** of the old.

For project-wide comment style and doc generation, see the **Comments** and **Documentation Generation** sections in the root [AGENTS.md](../AGENTS.md).

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

- `api/` -- 2 .c, 2 .h files
- `chains/` -- 102 .c, 50 .h files
- `cli/` -- 3 .c, 2 .h files
- `prover/` -- 1 .c, 1 .h files
- `server/` -- 21 .c, 5 .h files
- `util/` -- 13 .c, 14 .h files
- `verifier/` -- 1 .c, 1 .h files

<!-- AUTO:SRC_MODULE_INDEX:END -->
