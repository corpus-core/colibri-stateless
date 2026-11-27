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

The verifier exposes the generic C API:

```c
bool c4_verify_zk_proof(bytes_t proof, bytes_t public_inputs, const uint8_t* program_hash);
```

It hashes `public_inputs` exactly as supplied and binds the proof to the
registered Verification Key selected by `program_hash`. The Ethereum sync
application supplies a 168-byte `VerificationOutput`: the established
136-byte roots/period/header/domain prefix followed by the 32-byte SP1
`hash_bn254()` program hash. It reconstructs that final field from the
registered key before calling the generic verifier.

`verify_zk_proof(proof, public_inputs)` is the default Colibri application wrapper.
It additionally requires the current 168-byte layout and an appended identity equal
to the pinned program key. The native CLI and period-store artifact validation use
this wrapper, so an untrusted public-value file cannot choose another recursive identity.

## Workflow

1. **Generate Proof**: Use the Rust script in `../zk_proof/script` to generate a proof.
   ```bash
   cd ../zk_proof/script
   cargo run --release -- --prove
   ```
   Ensure you use the Groth16 wrapper if available in SP1 (requires Docker or Prover Network).

2. **Export VK**: Export the Verification Key from the Rust script.

3. **Verify in C**: Load the proof and VK in the C application and verify.


