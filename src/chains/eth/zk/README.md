# simple C-Function for ZK-Proof

## build

Building the C-Function can be done directly. Currently it still has a main-function for testing, but this can be removed. All dependencies are included using `#include`

```sh
clang  -O3 ../src/chains/eth/zk/verify_sync.c -o ./verify_sync_proof
```

## test

you can create the proof-data with either 

```sh
proof  eth_proof_sync 1365 > zk_input.ssz
```

or fetch it from the server with

```sh
curl --url https://c4.incubed.net/  --header 'content-type: application/json' --data '{"jsonrpc":"2.0","method":"eth_proof_sync", "params":["1365"]}' > zk_input.ssz
```

you can then run the verification-function with

```sh
./verify_sync_proof zk_input.ssz
``` 


