# Ethereum ZK Sync Verification (SP1)

This directory contains the SP1 zkVM implementation for verifying Ethereum Sync Committee updates.

## Structure

*   **`program/`**: The Rust guest program running inside the zkVM. Contains the logic to verify BLS signatures and Merkle proofs.
*   **`script/`**: The Rust host script. Generates the proof, handles inputs/outputs, and runs the prover.
*   **`common/`**: Shared data structures between Host and Guest.
*   **`export_vk/`**: Tool to export the Groth16 Verification Key (VK) and constants to a C header.

## Prerequisites

*   **Rust**: Install via `rustup`.
*   **SP1**: Install via `sp1up` (see [SP1 Docs](https://docs.succinct.xyz/)).
*   **Docker**: Required for generating Groth16 wrapped proofs. Ensure you have enough RAM allocated (e.g., >16GB).

## Workflow

### 1. Generate a Proof

Use the helper script `run_zk_proof.sh` in the `scripts/` folder at the project root.

```bash
# Generate a full Groth16 proof for period 1599
./scripts/run_zk_proof.sh --prove --groth16 --period 1599
```

This will:
1.  Compile the Guest program.
2.  Execute the Host script.
3.  Generate the proof (this takes time!).
4.  Save artifacts to `.zk_proofs/`:
    *   `proof_1599_groth16.bin`: SP1 proof wrapper.
    *   `proof_1599_raw.bin`: Raw compressed proof bytes (A, B, C) for the C-Verifier.
    *   `public_values_1599.bin`: Public outputs (Next Keys Root, Period).
    *   `vk_1599_groth16.bin`: Verification Key.

### 2. Update C-Verifier Constants

**⚠️ Important:** If you modify the Rust code in `program/`, the **Program Hash** changes. You must regenerate the C header constants.

1.  Rebuild the Guest binary (done automatically by `run_zk_proof.sh` or manually via `cargo build`).
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

This updates `zk_verifier_constants.h` with the new `VK_PROGRAM_HASH`.

### 3. C-Verifier

The C implementation is located in `src/chains/eth/zk_verifier/`.
It uses the **MCL** library for BN254 elliptic curve operations.

*   **Library**: `zk_verifier.c` / `.h`
*   **Dependencies**: `mcl`, `util` (internal lib).
*   **Build**: Controlled via `ETH_ZKPROOF` CMake option.

### Recursion (Planned)

To chain proofs (e.g., 1599 -> 1600):
1.  Generate a **Compressed Proof** for 1599 (not Groth16).
2.  Pass this proof + `public_values_1599` as input to the Prover for 1600.
3.  The Guest program must implement logic to verify the previous proof recursively.
4.  The output will be a new proof verifying the transition 1599 -> 1600 *and* the validity of 1599.
