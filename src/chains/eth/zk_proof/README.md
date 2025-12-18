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
Artifacts are saved to `build/default/.period_store/<PERIOD>/`:
*   `blocks.ssz`, `headers.ssz`, `lcb.ssz`, `lcu.ssz`, `sync.ssz`: cached light-client inputs (fetched once and re-used by Slave nodes).
*   `zk_proof.bin`: Compressed SP1 proof (used for recursion).
*   `zk_vk_raw.bin`: Verification Key for the compressed proof (also fed into recursion).
*   `zk_proof_g16.bin`: Raw Groth16 proof bytes (260-byte BN254 tuple, used by C/Solidity verifiers).
*   `zk_vk.bin`: Verification Key for the Groth16 wrapper (converted to C via `export_vk`).
*   `zk_pub.bin`: Public values written as: `current_keys_root (32) | next_keys_root (32) | next_period (u64 LE) | attested_header_root (32) | domain (32)`.

**`zk_pub.bin` layout (byte offsets):**
*   `0..31`: `current_keys_root` (trust anchor pubkey root)
*   `32..63`: `next_keys_root` (pubkey root for `next_period`)
*   `64..71`: `next_period` (little-endian `u64`)
*   `72..103`: `attested_header_root` (`hash_tree_root(attested_header.beacon)`)
*   `104..135`: `domain` (32 bytes, must start with `0x07000000`)

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

### Frozen Guest ELF & Deterministic VK

The Verification Key only stays stable if every proof is generated from **exactly the same guest ELF**.  
`run_zk_proof.sh` now looks for `src/chains/eth/zk_proof/program/elf/eth_sync_program`:

1.  If the file exists it is used as-is (no rebuild).
2.  Otherwise the guest is compiled once, copied to `program/elf/`, and the script warns you to commit it.

**Action item:** After the first successful build copy the ELF into `program/elf/eth_sync_program` (or let the script do it) and commit it so that every developer / prover node produces identical VKs.  
Whenever you intentionally update the ELF (e.g. after modifying `program/`), regenerate the Groth16 verifier constants via:

```bash
cd src/chains/eth/zk_proof/export_vk
cargo run --release -- \
  --solidity-path ~/.sp1/circuits/groth16/v5.0.0/Groth16Verifier.sol \
  --vk-path ../../../../../build/default/.period_store/<PERIOD>/zk_vk.bin \
  --output-path ../../../../eth/zk_verifier/zk_verifier_constants.h
```

### SP1 Prover Network

The script can offload proving to the SP1 Prover Network:

```bash
export SP1_PRIVATE_KEY=<network-api-key>
./scripts/run_zk_proof.sh --prove --groth16 --network --period 1600
```

Flags / Env vars:
*   `--network`: switches `SP1_PROVER=network`.
*   `--private-key <hex>`: optional inline key, otherwise `SP1_PRIVATE_KEY` must be set.
*   `CHAIN`: Selects chain-specific defaults. Supported: `mainnet` (default), `sepolia`, `gnosis`, `chiado`, `base`. Controls the default `EPOCHS_PER_PERIOD` (256 for mainnet/sepolia/base, 512 for gnosis/chiado) and, when available, a default Beacon RPC URL. You can still override `EPOCHS_PER_PERIOD` or `RPC_URL`.
*   `SP1_PRIVATE_KEY_FILE` / `NETWORK_PRIVATE_KEY_FILE`: optional paths to files containing the hex key (handy for Docker secrets). When set, `run_zk_proof.sh` reads the file and exports `SP1_PRIVATE_KEY`/`NETWORK_PRIVATE_KEY` automatically. If you mount a secret to `/run/secrets/sp1_private_key`, the script will pick it up without additional flags.
*   `SP1_SKIP_VERIFY=1`: skip the local Groth16 verification step (useful in containers where Docker isn’t available).

The host still builds/uses the same ELF and runs the orchestration logic locally—the heavy lifting happens remotely.

#### Using Docker secrets

Create a secret once on the host:

```bash
mkdir -p secrets
echo "0xYOUR_SP1_NETWORK_KEY" > secrets/sp1_private_key
```

Then mount it via Compose (see `docker-compose.example.yml`), for example:

```yaml
services:
  prover-daemon:
    environment:
      - CHAIN=mainnet
      - SP1_PRIVATE_KEY_FILE=/run/secrets/sp1_network_key
      - SP1_SKIP_VERIFY=1    # optional: skip local verification inside the container
    secrets:
      - sp1_network_key
secrets:
  sp1_network_key:
    file: ./secrets/sp1_private_key
```

`run_zk_proof.sh` and the daemon will read `/run/secrets/sp1_network_key` and export `SP1_PRIVATE_KEY` for you, so the key never appears in the plain environment. See `src/chains/eth/zk_proof/daemon/docker-compose.example.yml` for a full setup (volumes, metrics, etc.).

## C-Verifier Integration

The C implementation is located in `src/chains/eth/zk_verifier/`.
By default it uses the hand-rolled BN254 implementation under `src/chains/eth/bn254`.  
When Colibri is built with `-DUSE_MCL=1` the verifier instead uses **MCL** for accelerated pairings.

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
