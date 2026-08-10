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

## Verification Modules (`verifier/`)

| File | Purpose |
|------|---------|
| `op_verify.c` | Main OP verification dispatcher and method registration |
| `op_verify_account.c` | Account verification (OP-specific) |
| `op_verify_tx.c` | Transaction verification |
| `op_verify_block.c` | Block verification |
| `op_verify_receipt.c` | Receipt verification |
| `op_verify_logs.c` | Log verification |
| `op_verify_call.c` | Contract call verification |
| `op_verify_simulate.c` | Transaction simulation |
| `op_zstd.c` | ZSTD decompression for OP batch data |
| `op_chains_conf.c` | Chain configuration (L1 origin, system config) |

## Prover Modules (`prover/`)

| File | Purpose |
|------|---------|
| `op_prover.c` | Main OP prover dispatcher |
| `op_proof_account.c` | Account proof generation |
| `op_proof_transaction.c` | Transaction proof generation |
| `op_proof_receipt.c` | Receipt proof generation |
| `op_proof_logs.c` | Log proof generation |
| `op_proof_call.c` | Contract call proof generation |
| `op_proof_block.c` | Block proof generation |
| `op_tools.c` | OP utility functions |

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
| `op_types.c` | OP type implementations |

<!-- AUTO:OP_MODULE_INDEX:START -->

### Public Functions (auto-generated)

| File | Public Functions | Description |
|------|----------------|-------------|

<!-- AUTO:OP_MODULE_INDEX:END -->
