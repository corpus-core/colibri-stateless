: Ethereum

:: Benchmark

Verifying rpc-result may be a overheade, but the overhead my be a lot smaller than expected.

## Payload Size

The Size of a payload depend on the method.  Here are some examples comparing the payload of colibri stateless (using ssz) to a direct json-response from a roc-provider:

| method | colibri proof incl. data (SSZ) | RPC-Data (JSON) |
| ---------------- | -------------------------- | --------------------- |
| eth_getBlockByNumer  | 108 kB | 403 kB |
| eth_getTransactionByHash (Deploy)| 24 kB | 47 kB |
| eth_getTransactionByHash (Transfer)| 1.5 kB | 0.9 kB |
| eth_getTransactionReceipt | 1.5 kB | 1.6 kB |
| eth_getLogs | 53 kB | 15 kB |
| eth_call ( ERC20.balanceOf) | 17 kB | 0.1 kB |

So while a complete block is even smaller than the json-data (because colibri stateless packs in the binary (ssz) execution payload from the beacon-chain and extracts ), other methods such as eth_call require nerkle proofs for every storage value before execution the evm.

## Verification overhead

Verifying usualy is not a huge overhead. The most time consuming part is the checking the BLS-Signature.
These times in the table show the time used to create the proof and verify it. (the rpc-responses are taken from the filesystem in order to ignore latency issues). The numbers in bracket are the times for creating the proof and verifying it.

These Benchmarks come from tests on Apple M3 Max :

| method | native | JS (wasm)
| ---------------- | -------------------------- | --------------------- |
| eth_getBlockByNumber  | 24 ms (5+19) | 46 ms (5+41) |
| eth_getTransactionByHash | 16 ms (3+13) | 43 ms (8+35) |
| eth_getTransactionReceipt | 45 ms (43+2) | 68 ms (33+35) |
| eth_getLogs | 19 ms (17+2) | 47 ms (12+35) |
| eth_call ( ERC20.balanceOf) | 12 ms (10+2) | 51 ms (11+40) |


## Code size

Using cmake-defines you can adjust the codesize by only adding what you need. This is relevant especially for embedded devices.

Here are some examples of code sizes you may expect:


| config | code (TXT) | DATA | BSS | Total |
| ---------------- | ------ | ------ | ------ | ------ |
| FullImage for embedded Device(RTOS) (including OS) only for verifying tx receipts  | 200 kB | 35kB | 1.7kB | 236 kB |
| Native build on Mac (ARM) with full evm and curl | 281 kB | 177kB | 1.3kB | 460kB |
| Native build on Mac (ARM) only for tx verification | 122 kB | 64kB | 1.3kB | 188kB |
| WASM (all features) | 660 kB (WASM)| - | - | 980 kB (including js-glue code)|




