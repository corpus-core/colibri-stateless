# src/chains/eth/ - Ethereum Chain Module

Ethereum-specific implementation of the prover and verifier. Handles all Ethereum RPC methods, SSZ type definitions for proofs, Beacon Chain integration, and EVM execution.

## Directory Structure

| Directory | Purpose |
|-----------|---------|
| `verifier/` | Proof verification modules -- one file per RPC method category |
| `prover/` | Proof generation modules -- collects data from RPC/Beacon API |
| `ssz/` | SSZ type definitions for Ethereum proofs and data types |
| `server/` | Server-specific handlers (period store, head watching, metrics) |
| `precompiles/` | EVM precompile implementations (BLS, EC, Blake2, KZG) |
| `bn254/` | BN254 curve operations for ZK proofs |
| `zk_verifier/` | ZK proof verification (verifies SP1 proofs) |
| `zk_proof/` | ZK proof generation (Rust/SP1-based) |
| `cp_signer/` | Checkpoint signer CLI tool |

## Verification Modules (`verifier/`)

Each verification module handles a category of RPC methods. The main dispatcher is `eth_verify.c`.

| File | RPC Methods | Description |
|------|-------------|-------------|
| `eth_verify.c` | -- | Main dispatcher, method registration, chain type detection |
| `verify_account_proof.c` | `eth_getBalance`, `eth_getCode`, `eth_getStorageAt`, `eth_getProof`, `eth_getTransactionCount` | Account state verification via Merkle Patricia proofs |
| `verify_tx_proof.c` | `eth_getTransactionByHash`, `eth_getTransactionByBlockHashAndIndex`, `eth_getTransactionByBlockNumberAndIndex` | Transaction existence proof via transaction trie |
| `verify_block.c` | `eth_getBlockByNumber`, `eth_getBlockByHash`, `eth_blockNumber` | Block header verification against beacon chain |
| `verify_receipt_proof.c` | `eth_getTransactionReceipt` | Receipt verification via receipt trie |
| `verify_logs_proof.c` | `eth_getLogs` | Log verification (receipts + bloom filter) |
| `verify_call.c` | `eth_call`, `eth_estimateGas` | Stateless EVM execution via evmone |
| `verify_simulate.c` | `colibri_simulateTransaction` | Transaction simulation |
| `verify_local.c` | `eth_chainId`, `web3_sha3`, `net_version` | Local methods (no proof needed) |

### Supporting Verifier Files

| File | Purpose |
|------|---------|
| `sync_committee.c/h` | Verify BLS aggregate signatures from sync committee |
| `sync_committee_state.c` | Manage sync committee state (bootstrap, updates) |
| `beacon_header.c` | Verify beacon block headers against sync committee |
| `eth_account.c/h` | Account state handling and Merkle proof verification |
| `eth_tx.c/h` | Transaction parsing (Legacy, EIP-2930, EIP-1559, EIP-4844) |
| `patricia_trie.c` | Merkle Patricia Trie proof verification |
| `patricia.c` | Patricia tree implementation |
| `rlp.c` | RLP encoding/decoding |
| `state_overrides.c/h` | State override support for `eth_call` |
| `call_evmone.c` | EVM execution via evmone library |

## Prover Modules (`prover/`)

Each prover module generates proofs for a category of RPC methods. The main dispatcher is `eth_prover.c`.

| File | Purpose |
|------|---------|
| `eth_prover.c` | Main prover dispatcher |
| `proof_account.c` | Account proof generation (calls `eth_getProof`) |
| `proof_transaction.c` | Transaction proof generation |
| `proof_receipt.c` | Receipt proof generation |
| `proof_logs.c` | Log proof generation |
| `proof_call.c` | Contract call proof (collects access lists + state) |
| `proof_block.c` | Block proof generation |
| `proof_sync.c` | Sync committee proof generation (light client updates) |
| `proof_witness.c` | Witness data generation (for L2) |
| `historic_proof.c` | Historical block proof (older than 8192 slots) |
| `historic_proof_zk.c` | ZK-based historical proofs |
| `beacon.c/h` | Beacon API integration (bootstrap, updates, block roots) |
| `eth_req.c/h` | Request parsing and validation |
| `eth_tools.c/h` | Ethereum utility functions |

## SSZ Type Definitions (`ssz/`)

Type definitions follow the SSZ encoding standard. Each type is a `ssz_def_t[]` array.

| File | Types |
|------|-------|
| `verify_proof_types.h` | Proof types: `C4RequestAccountProof`, `C4RequestTxProof`, `C4RequestBlockProof`, etc. |
| `verify_data_types.h` | Data types: account data, transaction data, block data, etc. |
| `verify_types.c` | Implementation of verification type arrays |
| `beacon_types.h/c` | Beacon chain types: `BeaconBlockHeader`, `SyncCommittee`, etc. |
| `beacon_denep.c` | Deneb fork types (blobs, KZG) |
| `beacon_electra.c` | Electra fork types |

## Key Concepts

### Verification Flow

1. Proof arrives as SSZ-encoded `C4Request` containing: `data`, `proof`, `sync_data`.
2. `sync_data` contains `LightClientUpdate` with sync committee BLS signatures.
3. Verify sync committee signatures against known committee state.
4. Extract beacon block header from proof, verify it matches the signed header.
5. Extract execution payload root, verify it matches the execution block.
6. Apply method-specific verification (e.g., Merkle Patricia proof for accounts).

### Sync Committee

The verifier stores one piece of state: the current sync committee (512 BLS public keys, rotating every ~27 hours / 256 epochs). Light client updates prove committee transitions. Bootstrap establishes the initial committee from a trusted checkpoint.

### Fork Support

The codebase handles multiple Ethereum consensus forks (Deneb, Electra) through SSZ type variants. Fork detection is based on slot numbers.

<!-- AUTO:ETH_MODULE_INDEX:START -->

### Public Functions (auto-generated)

| File | Public Functions | Description |
|------|----------------|-------------|

<!-- AUTO:ETH_MODULE_INDEX:END -->
