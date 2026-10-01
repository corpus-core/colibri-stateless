# src/cli/ - Command-Line Tools

Three standalone executables that wrap the prover and verifier libraries for shell scripts, cron jobs, tests, and local development. Built when CMake option `CLI=ON` (default). Disabled for `WASM` and `SWIFT` targets.

GitBook / user-facing docs are generated from `// :` / `// ::` / `// :::` comments in the `.c` sources via `scripts/doc/index.js`. Do not rewrite those comment blocks without prior agreement.

## Tools

| Binary | Source | Symlink / alias | Purpose |
|--------|--------|-----------------|---------|
| `colibri-prover` | `prover.c` | — | Generate SSZ proofs for an RPC method + params |
| `colibri-verifier` | `verifier.c` | `colibri` | Verify a local proof, or fetch+verify via prover / local mode |
| `colibri-ssz` | `ssz.c` | `ssz` | Decode SSZ (proofs or typed blobs) to JSON / hash_tree_root |

Binaries land in `${CMAKE_BINARY_DIR}/bin/` (e.g. `build/default/bin/`).

### Quick examples

```bash
# Proof for latest block header (writes SSZ)
colibri-prover -o block_proof.ssz eth_getBlockByNumber latest false

# Verify that proof (or omit -i and use default remote prover)
colibri-verifier -i block_proof.ssz eth_getBlockByNumber latest false

# Inspect proof as JSON
colibri-ssz -o block.json block_proof.ssz
```

## Architecture

```
  argv (method + params / proof file)
           │
           ▼
    ┌──────────────┐     C4_PENDING      ┌─────────────┐
    │ colibri-*    │ ──────────────────► │ curl_fetch  │  (USE_CURL)
    │  main loop   │ ◄────────────────── │ (RPC/CL/…)  │
    └──────┬───────┘                     └─────────────┘
           │
     ┌─────┴─────┐
     ▼           ▼
  prover.h    verify.h / api (c4_rpc_*)
     │           │
     └─────┬─────┘
           ▼
     chains/eth (+ op) + util/
```

Each tool owns a simple host loop: call execute until `C4_SUCCESS` / `C4_ERROR`; on `C4_PENDING`, fill pending `data_request_t` entries (via libcurl when `CURL=ON`).

## Key Files

| File | Purpose |
|------|---------|
| `prover.c` | `colibri-prover` entry: flags, `c4_prover_create` / `c4_prover_execute`, write proof |
| `verifier.c` | `colibri-verifier` / `colibri` entry: local file or remote/local/hybrid prove+verify via `c4_rpc_*` |
| `ssz.c` | `colibri-ssz` / `ssz` entry: type resolve, field path, JSON / hash / hex dump |
| `config.h` | Shared helpers: default chain config from `default_chains.generated.h`, URL overrides (`set_config`) |
| `default_chains.generated.h` | Generated chain name → id and default JSON endpoints (do not edit) |
| `CMakeLists.txt` | Builds/installs the three binaries and symlinks |

### Generated defaults

`default_chains.generated.h` is produced from `scripts/chain_defaults/chains.json`:

```bash
node scripts/chain_defaults/generate.js
node scripts/chain_defaults/generate.js --check
```

## Configuration

Endpoint / backend settings (when `CURL=ON`):

1. `C4_CONFIG` environment variable (path to JSON), or
2. `./c4_config.json` in the current working directory, or
3. Built-in defaults from `default_chains.generated.h` (selected via `-c`)

Example `c4_config.json`:

```json
{
  "eth_rpc": ["https://…"],
  "beacon_api": ["https://…"],
  "checkpointz": ["https://…"],
  "prover": ["https://…"]
}
```

CLI flags can override pieces of that config (e.g. verifier `-r` / `-b` / `-p` / `-x`).

### Storage

With `FILE_STORAGE=ON`, prover and verifier prefer the file storage plugin so sync-committee state persists under the states directory (verifier `-s` when available).

## Tool reference (agent-oriented)

User-facing option tables live in the source `// :::` comments. Summary for agents:

### colibri-prover

- **Input:** `<method> <params…>` (params become a JSON array; objects/arrays/`true`/`false` pass through raw).
- **Output:** SSZ proof to `-o` file or stdout.
- **Notable flags:** `-c` chain id, `-o` output, `-i` include code, `-G` logs completeness, `-N` Nimbus, `-D` Lodestar, `-d` chain_store (CURL), `-t`/`-x` test/cache dirs (TEST+CURL).

### colibri-verifier

- **Input:** optional `-i <proof.ssz>` (or HTTP URL treated as prover), else method+args with prover mode.
- **Modes (`-m`):** `remote` (default), `local`, `hybrid`; `-L` = local.
- **Notable flags:** `-c` chain name/id, `-C` trusted checkpoint, `-P` PAP, `-W` skip WSP (security-sensitive), `-A` max age for `"latest"`, `-G`/`-N`/`-D` as on prover, `-Z` oblivious node, `-n` checkpoint signers (CURL).

### colibri-ssz

- **Input:** SSZ file; optional `-t <typename>` for non-proof blobs (`signedblock`, `lcu`, `lcb`, `zk`, `txcache`, …).
- **Output:** JSON (`-o`), optional hash_tree_root (`-h`), type name (`-n`), hex serialization (`-s`); trailing args select container fields / list indices.

## Build & install

```bash
cmake --preset default   # CLI=ON
cmake --build build/default
./build/default/bin/colibri-prover --version
./build/default/bin/colibri-verifier --help
```

CMake option: `CLI` (default ON). Linking uses `prover` / `verifier` / chain modules; with `CURL=ON` also `curl_fetch`. Install places binaries under `${CMAKE_INSTALL_BINDIR}` and creates `colibri` → `colibri-verifier` and `ssz` → `colibri-ssz` (copy on Windows).

## Dependencies

| Target | Typical link libs |
|--------|-------------------|
| `colibri-prover` | `prover`, `verifier` (+ `curl_fetch` if CURL) |
| `colibri-verifier` | `api`, `prover`, `verifier`, `eth_verifier`, `eth_prover` (+ `curl_fetch`) |
| `colibri-ssz` | `verifier`, `prover`, `eth_verifier` |

Related modules: [../prover/](../prover/), [../verifier/](../verifier/), [../api/](../api/), [../util/AGENTS.md](../util/AGENTS.md), [../chains/eth/AGENTS.md](../chains/eth/AGENTS.md).
