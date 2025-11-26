: Ethereum

:: Benchmark

Verifying RPC results introduces computational overhead, but in practice the cost is often significantly lower than expected.  
Proof verification typically requires only a few hash computations and a single BLS signature check, which is negligible compared to the cost of executing or syncing full node data.

## Payload Size

The size of a payload depend on the method.  Here are some examples comparing the payload of **colibri.stateless** (using ssz) to a direct json-response from a RPC-provider:

| method | colibri proof incl. data (SSZ) | RPC-Data (JSON) |
| ---------------- | -------------------------- | --------------------- |
| eth_getBlockByNumer  | 108 kB | 403 kB |
| eth_getTransactionByHash (Deploy)| 24 kB | 47 kB |
| eth_getTransactionByHash (Transfer)| 1.5 kB | 0.9 kB |
| eth_getTransactionReceipt | 1.5 kB | 1.6 kB |
| eth_getLogs | 53 kB | 15 kB |
| eth_call ( ERC20.balanceOf) | 17 kB | 0.1 kB |

So, while a complete block is often smaller than the corresponding JSON-RPC data (because **colibri.stateless** uses the binary **SSZ**-encoded execution payload from the beacon chain and extracts it directly), other methods such as `eth_call` require additional Merkle proofs for every accessed storage value before the EVM execution can be verified.

## Verification overhead

Verification usualy is not a huge overhead. The most time consuming part is checking the BLS-Signature.

The times shown in the table represent the total time required to generate and verify each proof.  
RPC responses were read from the local filesystem to eliminate network latency effects.  
Numbers in brackets indicate the separate durations for proof creation and verification.

These benchmarks were obtained from tests performed on an **Apple M3 Max** system:

| method | native | JS (wasm)
| ---------------- | -------------------------- | --------------------- |
| eth_getBlockByNumber  | 24 ms (5+19) | 46 ms (5+41) |
| eth_getTransactionByHash | 16 ms (3+13) | 43 ms (8+35) |
| eth_getTransactionReceipt | 45 ms (43+2) | 68 ms (33+35) |
| eth_getLogs | 19 ms (17+2) | 47 ms (12+35) |
| eth_call ( ERC20.balanceOf) | 12 ms (10+2) | 51 ms (11+40) |

## Code size

Using **CMake-defines**, the code size can be adjusted by including only the required components.  
This is particularly relevant for embedded devices, where memory and storage resources are limited.

Here are some examples of code sizes you may expect:


| config | code (TXT) | DATA | BSS | Total |
| ---------------- | ------ | ------ | ------ | ------ |
| FullImage for embedded Device(RTOS) (including OS) only for verifying tx receipts  | 200 kB | 35kB | 1.7kB | 236 kB |
| Native build on Mac (ARM) with full evm and curl | 281 kB | 177kB | 1.3kB | 460kB |
| Native build on Mac (ARM) only for tx verification | 122 kB | 64kB | 1.3kB | 188kB |
| WASM (all features) | 660 kB (WASM)| - | - | 980 kB (including js-glue code)|




