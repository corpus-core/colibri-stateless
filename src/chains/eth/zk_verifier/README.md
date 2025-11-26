# ZK Verifier (C Implementation)

This directory will contain the C implementation of the Groth16 verifier for the SP1 proof.

## Requirements

The SP1 proof is a Groth16 proof over the BN254 (also known as BN128 or alt_bn128) curve.
To verify this in C, you need a library that supports BN254 pairings.

Recommended options for Embedded/WASM:
1. **MCL**: https://github.com/herumi/mcl (Highly optimized, supports WASM and many archs)
2. **Relic Toolkit**: https://github.com/relic-toolkit/relic (Configurable, used in many crypto projects)
3. **Micro-ECC** (if it supports pairings, usually only ECDSA) - likely not enough.

## Interface

The verifier should export a function like:

```c
bool verify_sp1_proof(const uint8_t* proof, size_t proof_len, const uint8_t* public_inputs);
```

## Workflow

1. **Generate Proof**: Use the Rust script in `../zk_proof/script` to generate a proof.
   ```bash
   cd ../zk_proof/script
   cargo run --release -- --prove
   ```
   Ensure you use the Groth16 wrapper if available in SP1 (requires Docker or Prover Network).

2. **Export VK**: Export the Verification Key from the Rust script.

3. **Verify in C**: Load the proof and VK in the C application and verify.


