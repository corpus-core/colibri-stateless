# src/chains/op/ - OP-Stack Chain Module

OP-Stack (Optimism, Base, etc.) chain module. Proofs reuse the Ethereum SSZ types
and ETH verify/prover implementations. The only OP-specific wire difference is
`ETH_BLOCK_PROOF_UNION` index 2 (`sequencerProof`).

## Directory Structure

| Directory | Purpose |
|-----------|---------|
| `verifier/` | Sequencer-proof verification, ZSTD, chain config |
| `prover/` | Preconf fetch + `sequencerProof` builder; dispatches to ETH `c4_proof_*` |
| `server/` | Server handlers (preconf capture, configuration) |
| `kona_bridge/` | Rust-based P2P bridge for preconfirmation capture |

## Key Differences from Ethereum Module

1. **Sequencer proofs**: non-hybrid proofs authenticate the EL block via a sequencer-signed execution payload (`sequencerProof`), not a beacon `clProof`.
2. **ZSTD Compression**: the sequencer payload may be ZSTD-compressed (`[parentBeaconRoot | SSZ execution_payload]`).
3. **Header cache**: after the first verified block, follow-up proofs use `blockHash` like ETH.
4. **Kona Bridge**: Native P2P bridge (Rust/FFI) connects to OP-Stack sequencers for live preconf capture.

## Verification Modules (`verifier/`)

| File | Purpose |
|------|---------|
| `op_verify.c` | Dispatcher: registers the verify hook, then `c4_eth_dispatch_proof` |
| `op_verify_block.c` | `op_verify_sequencer_proof` (sig, ZSTD, RLP, keccak bind, header cache) |
| `op_zstd.c` | ZSTD decompression |
| `op_chains_conf.c` | Sequencer addresses and (prover) chain endpoints |

## Prover Modules (`prover/`)

| File | Purpose |
|------|---------|
| `op_prover.c` | Registers hooks, then calls ETH `c4_proof_*` |
| `op_proof_block.c` | `op_get_el_block` (preconf) + `op_add_sequencer_proof` |

## Kona Bridge (`kona_bridge/`)

Rust-based P2P bridge that connects to OP-Stack sequencers using native protocols.

**Architecture:**
```
C Server <--FFI--> Kona Bridge (Rust) <--discv5+GossipSub--> OP Sequencers
```

**Supported chains:** OP Mainnet (10), Base (8453), and other OP-Stack chains.

<!-- AUTO:OP_MODULE_INDEX:START -->

### Public Functions (auto-generated)

| File | Public Functions | Description |
|------|----------------|-------------|

<!-- AUTO:OP_MODULE_INDEX:END -->
