# Dart Examples

This folder contains runnable Dart examples for the Colibri bindings.

## Prerequisites

1) Build the native library:

```bash
cd bindings/dart
./build.sh
```

2) Install Dart dependencies:

```bash
dart pub get
```

## Examples

### 1) Basic usage

`basic_usage.dart` shows a minimal verified RPC call:

```bash
dart run example/basic_usage.dart
```

### 2) Manual proof + verify

`proof_verify.dart` shows how to create a proof locally and verify it:

```bash
dart run example/proof_verify.dart
```

### 3) Custom storage

`custom_storage.dart` registers a storage backend for native caching:

```bash
dart run example/custom_storage.dart
```

### 4) Unproofable RPC

`unproofable_rpc.dart` calls a method that is fetched directly from RPC:

```bash
dart run example/unproofable_rpc.dart
```

### 5) Read a block

`read_block.dart` reads a block by number and prints key fields:

```bash
dart run example/read_block.dart
```

### 6) Transaction receipt

`transaction_receipt.dart` fetches and prints a receipt:

```bash
dart run example/transaction_receipt.dart
```

### 7) Read logs

`read_logs.dart` fetches logs using a filter:

```bash
dart run example/read_logs.dart
```

### 8) Contract call

`contract_call.dart` performs a read-only `eth_call`:

```bash
dart run example/contract_call.dart
```

## Native library path

By default, the examples load the library from `bindings/dart/native`:

- macOS: `native/libcolibri.dylib`
- Linux: `native/libcolibri.so`
- Windows: `native/colibri.dll`

To override the path, set the environment variable:

```bash
export COLIBRI_DART_LIBRARY=/full/path/to/libcolibri.(dylib|so|dll)
```

## Optional .env configuration

Examples can load settings from `bindings/dart/.env`:

```bash
cp .env.example .env
```

Supported keys:

- `COLIBRI_DART_LIBRARY` (native library path)
- `COLIBRI_PROVER` / `COLIBRI_PROVERS` (prover service URLs)
- `COLIBRI_ETH_RPC` / `COLIBRI_ETH_RPCS` (Ethereum RPC URLs)
- `COLIBRI_ZK_PROOF` (set `true` to request ZK sync proofs from the prover)
- `COLIBRI_CHECKPOINT_WITNESS_KEYS` (hex concatenated signer addresses for ZK proofs)
- `COLIBRI_DEBUG_ZK` (set `true` to log prover request details)

### Run examples with ZK proofs

To force ZK sync proofs for the examples, set:

```bash
export COLIBRI_ZK_PROOF=true
export COLIBRI_PROVER=https://mainnet.colibri-proof.tech
export COLIBRI_CHECKPOINT_WITNESS_KEYS=0x07f50c1d17cb84a656692ddfd577c09756cb305b
```

Then run any example:

```bash
dart run example/basic_usage.dart
```
