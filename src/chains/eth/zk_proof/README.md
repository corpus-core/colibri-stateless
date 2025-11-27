# Ethereum ZK Sync Verification (SP1)

This directory contains the SP1 zkVM implementation for verifying Ethereum Sync Committee updates.
It enables the creation of Zero-Knowledge Proofs (ZKP) that attest to the validity of Light Client updates, allowing for extremely lightweight verification on embedded devices or smart contracts.

## Structure

*   **`program/`**: The Rust guest program running inside the zkVM. Contains the logic to verify BLS signatures, reconstruct Merkle roots, and validate chain transitions.
*   **`script/`**: The Rust host script. Handles fetching data, generating proofs (Core & Groth16), and managing recursion.
*   **`common/`**: Shared data structures between Host and Guest (`SP1GuestInput`, `VerificationOutput`).
*   **`export_vk/`**: Tool to export the Groth16 Verification Key (VK) and constants to a C header for the embedded verifier.

## Prerequisites

*   **Rust**: Install via `rustup`.
*   **SP1**: The `run_zk_proof.sh` script will attempt to install SP1 if missing (via `sp1up`), but you can manually install it using the instructions in the [SP1 Docs](https://docs.succinct.xyz/).
*   **Docker**: Required for generating Groth16 wrapped proofs. Ensure you have enough RAM allocated (e.g., >16GB).
*   **macOS/Linux**: The scripts are designed for Unix-like environments.

## Usage: `run_zk_proof.sh`

The primary interface is the `scripts/run_zk_proof.sh` helper script located at the project root.

### Arguments

*   `--period <N>`: The sync committee period to verify.
*   `--prove`: Performs actual proof generation (omitting this runs in fast simulation mode).
*   `--groth16`: Wraps the STARK proof in a SNARK (Groth16) for constant-size on-chain/embedded verification.
*   `--rpc <url>`: Optional. URL to fetch Beacon API data from (Default: `https://mainnet1.colibri-proof.tech/`).
*   `--output <dir>`: Optional. Output directory for artifacts (Default: `.zk_proofs`).

### 1. Generate a Single Proof

To generate a proof for a specific period (e.g., 1600):

```bash
./scripts/run_zk_proof.sh --prove --groth16 --period 1600
```

*   `--prove`: Performs actual proof generation (omitting this runs in fast simulation mode).
*   `--groth16`: Wraps the STARK proof in a SNARK (Groth16) for constant-size on-chain/embedded verification.
*   `--period <N>`: The sync committee period to verify.

**Output:**
Artifacts are saved to `.zk_proofs/`:
*   `proof_1600_groth16.bin`: SP1 Groth16 proof (for use with the C-Verifier).
*   `proof_1600_raw.bin`: Raw compressed proof bytes (A, B, C points) extracted from the Groth16 proof.
*   `public_values_1600.bin`: The public inputs/outputs (Trust Anchor, New Keys, Period).
*   `vk_1600_groth16.bin`: The Verification Key used for this proof.

### 2. Generate a Recursive Chain (Loop Mode)

To generate a chain of proofs (e.g., 1599 -> 1600 -> 1601), where each proof verifies the previous one:

```bash
./scripts/run_zk_proof.sh --start-period 1599 --end-period 1601 --prove --groth16
```

*   **Step 1 (1599)**: Generates the initial proof (Anchor -> 1600).
*   **Step 2 (1600)**: Takes the *output* of 1599 as *input*. Generates a recursive proof (Anchor -> 1601).
*   **Aggregation**: The final public values will show a transition from the *original* Trust Anchor (start of 1599) to the final state (end of 1601).

### 3. Update C-Verifier Constants

**⚠️ Important:** If you modify the Rust code in `program/`, the **Program Hash** changes. You must regenerate the C header constants to allow the C-Verifier to accept the new proofs.

1.  Rebuild the Guest binary (done automatically by `run_zk_proof.sh`).
2.  Run the `export-vk` tool:

```bash
# Find the compiled ELF binary
ELF=$(find src/chains/eth/zk_proof/target/riscv32im-succinct-zkvm-elf/release/deps -name "eth_sync_program*" -type f -not -name "*.*" | head -n 1)

# Run export
cargo run --manifest-path src/chains/eth/zk_proof/export_vk/Cargo.toml -- \
  --solidity-path ~/.sp1/circuits/groth16/v5.0.0/Groth16Verifier.sol \
  --elf-path "$ELF" \
  --output-path src/chains/eth/zk_verifier/zk_verifier_constants.h
```

## C-Verifier Integration

The C implementation is located in `src/chains/eth/zk_verifier/`.
It uses the **MCL** library for BN254 elliptic curve operations to verify the generated Groth16 proofs.

*   **Library**: `zk_verifier.c` / `.h`
*   **Build**: Controlled via `ETH_ZKPROOF` CMake option (Default: ON).
*   **Interface**: `bool verify_zk_proof(bytes_t proof, bytes_t public_inputs);`

## Recursion & Aggregation

The Guest Program supports recursion to create "Update Chains".
*   **Input**: `RecursionData` (Previous Proof + Previous Public Values).
*   **Logic**:
    1.  Verifies the previous SP1 proof using `sp1_zkvm::lib::verify::verify_sp1_proof`.
    2.  Checks that the `next_keys_root` of the previous proof matches the `current_keys_root` of the current update.
    3.  **Aggregates**: Outputs the `current_keys_root` from the *previous* proof as its own `current_keys_root`.
*   **Result**: A single proof attesting to the valid transition from `Period N` to `Period N+k`.
