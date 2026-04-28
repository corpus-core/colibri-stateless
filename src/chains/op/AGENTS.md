# src/chains/op/ - OP-Stack Chain Module

OP-Stack (Optimism, Base, etc.) chain module. Similar structure to the Ethereum module but with OP-specific features: preconfirmations, ZSTD compression, and a Kona P2P bridge.

## Directory Structure

| Directory | Purpose |
|-----------|---------|
| `verifier/` | OP-Stack proof verification |
| `prover/` | OP-Stack proof generation |
| `ssz/` | OP-Stack SSZ type definitions |
| `server/` | Server handlers (preconf capture, configuration) |
| `kona_bridge/` | Rust-based P2P bridge for preconfirmation capture |

## Key Differences from Ethereum Module

1. **Preconfirmations**: OP-Stack uses preconfirmations for faster finality. The prover can include preconf data in proofs.
2. **ZSTD Compression**: OP proofs use ZSTD compression for batch transaction data.
3. **Chain Configuration**: OP-Stack chains have different configurations (L1 origin, system config, etc.) managed in `op_chains_conf.c`.
4. **Kona Bridge**: Native P2P bridge (Rust/FFI) connects to OP-Stack sequencers for live preconf capture.
5. **Shared EL stack**: Execution-layer proofs and hybrid SSZ layouts match Ethereum; OP-specific code only covers preconf payloads and sequencer verification (`op_payload.c`, `op_verify_block.c`).

## Verification Modules (`verifier/`)

| File | Purpose |
|------|---------|
| `op_verify.c` | OP method registration, legacy `OpBlockProof` branch, else delegates to `c4_eth_dispatch_execution_proof` |
| `op_verify_block.c` | Preconf / non-hybrid `OpBlockProof` verification (sequencer + ZSTD) |
| `op_zstd.c` | ZSTD decompression for OP batch data |
| `op_chains_conf.c` | Chain configuration (L1 origin, system config) |

Hybrid and other execution-layer proofs use **Ethereum verifier** implementations (`verify_*` in `chains/eth/verifier/`).

## Prover Modules (`prover/`)

| File | Purpose |
|------|---------|
| `op_prover.c` | Thin wrapper delegating to `eth_prover_execute()` for EL proofs |
| `op_block_fetch.c` | Hybrid + preconf execution/block loading (`c4_op_hybrid_*`, `c4_op_preconf_*`) consumed via `chains/eth/prover/beacon.c` |
| `op_tools.c` | OP SSZ/version helpers (`op_create_proof_request`, etc.) |

## Kona Bridge (`kona_bridge/`)

Rust-based P2P bridge that connects to OP-Stack sequencers using native protocols.

**Architecture:**
```
C Server <--FFI--> Kona Bridge (Rust) <--discv5+GossipSub--> OP Sequencers
```

**Key points:**
- Uses `kona-p2p` (Rust) for OP-Stack-compatible P2P networking.
- discv5 discovery with ENR bootnodes (same as real sequencers).
- GossipSub for preconfirmation distribution.
- C integration via FFI: `kona_bridge_start()` / `kona_bridge_stop()`.
- Captures preconfirmations and writes them to files or passes to C server.

**Supported chains:** OP Mainnet (10), Base (8453), and other OP-Stack chains.

## SSZ Types (`ssz/`)

| File | Purpose |
|------|---------|
| `op_proof_types.h` | OP proof type definitions (extends ETH types with preconf fields) |
| `../eth/ssz/verify_types.c` | Shared SSZ defs including `C4_OP_REQUEST_PROOFS_UNION` and `op_ssz_verification_type()` |

<!-- AUTO:OP_MODULE_INDEX:START -->

### Public Functions (auto-generated)

| File | Public Functions | Description |
|------|----------------|-------------|

<!-- AUTO:OP_MODULE_INDEX:END -->
