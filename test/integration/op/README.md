# OP integration tests (planned / manual)

Full end-to-end OP-hybrid flows (remote prover + sequencer preconf verification) require live RPC endpoints and are not run in CI by default.

Coverage in-tree:

- `test/unittests/test_op_verify_methods.c` — RPC method classification for OP (`eth_getBlockReceipts`, `eth_getBlockHeader`, blob base fee, priority fee).
- Existing ETH verifier/prover tests exercise shared hybrid proof code paths linked by `op_verifier` / `eth_prover`.

To add scripted integration tests later, mirror `scripts/create_test.sh` workflows used for Ethereum hybrid proofs against an OP Sepolia/Base RPC and a configured remote prover.
