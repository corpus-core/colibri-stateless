# Ethereum Sync Committee ZK Proof (SP1)

This project implements a Zero-Knowledge Proof for Ethereum Sync Committee transitions using SP1.

## Structure

- `program/`: The Guest program (Rust) that runs inside the zkVM. It verifies BLS signatures and Merkle proofs.
- `script/`: The Host script (Rust) that prepares data, executes the guest (proof generation), and verifies the result.
- `common/`: Shared data structures and logic (like SHA256 Merkle verification) used by both guest and host.

## Prerequisites

- Rust (via `rustup`)
- SP1 Toolchain (`sp1up`)

## Usage

We provide a helper script to handle the build process (including the specific toolchain for the guest and the standard toolchain for the host) and execution.

### 1. Prepare Input Data
Ensure you have a `zk_input.ssz` file in this directory or `src/chains/eth/zk_proof/script/`. The script will try to copy it from `build/default/sync_1599.ssz` if available.

### 2. Run Verification (Execute Mode)
This runs the proof logic in the SP1 simulator (fast, no proof generation).

```bash
./run_zk_proof.sh
```

### 3. Generate Proof (Prove Mode)
To actually generate a Groth16 proof (requires Docker or Prover Network access), you need to modify the script call in `run_zk_proof.sh` to use `--prove` instead of `--execute`.

```bash
# In run_zk_proof.sh, change:
# "$HOST_BINARY" --execute --input-file zk_input.ssz
# to:
# "$HOST_BINARY" --prove --input-file zk_input.ssz
```

## Troubleshooting

- **Toolchain errors**: Ensure `sp1up` ran successfully.
- **Linker errors**: The helper script manages `RUSTFLAGS` to avoid conflicts between Guest (RISC-V) and Host (native) builds. Use the script instead of `cargo build` directly.

