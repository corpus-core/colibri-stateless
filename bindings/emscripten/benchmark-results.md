## Latency

Measured using the WASM client against `mainnet1.colibri-proof.tech`.  
3 blocks, 5 runs per block.

| Method | local | lightclient | remote | unverified |
|--------|------|------|------|------|
| eth_blockNumber | 138 | 0 | 51 | 26 |
| eth_getBlockByNumber(false) | 171 | 8 | 97 | 27 |
| eth_getBlockByNumber(true) | 347 | 623 | 285 | 56 |
| eth_call | 204 | 48 | 56 | 23 |
| eth_getLogs | 328 | 209 | 212 | 30 |
| eth_getBalance | 153 | 18 | 50 | 22 |
| eth_getTransactionByHash(latest) | 180 | 52 | 66 | 21 |
| eth_getTransactionByHash(historic) | - | 17 | 113 | 24 |
| eth_getTransactionReceipt(latest) | 246 | 124 | 90 | 24 |
| eth_getTransactionReceipt(historic) | - | 18 | 117 | 22 |

## PAP Impact on Latency 

| Method | local | lightclient | remote |
|--------|------|------|------|
| eth_call | +11 / +278 | +59 / +705 | -2 / +138 |
| eth_getLogs | +17 | -14 | +127 |
| eth_getBalance | +6 | +0 | +2 |
| eth_getTransactionByHash(latest) | -6 | +40 | +25 |
| eth_getTransactionByHash(historic) | - | +87 | -6 |
| eth_getTransactionReceipt(latest) | -1 | -11 | +74 |
| eth_getTransactionReceipt(historic) | - | +29 | +36 |
**Note:**: eth_call will require additional request when used for the first time to fill the cache, so a cold-request may take a bit longer, butrequesting the same or simiular data, will be very fast, since the storage is taken from the cache and verified afterwards.


### Transfer Size (avg, kB)

The size of a payload depend on the method. **colibri.stateless** uses ssz while json-rpc returs a json-response from a RPC-provider:

| Method | local | lightclient | remote | unverified |
|--------|------|------|------|------|
| eth_blockNumber | 353.7 | 0.0 | 0.7 | 0.0 |
| eth_getBlockByNumber(false) | 353.7 | 40.6 | 146.4 | 29.0 |
| eth_getBlockByNumber(true) | 365.5 | 0.0 | 146.4 | 576.2 |
| eth_call | 460.6 | 95.1 | 27.7 | 0.1 |
| eth_getLogs | 1062.6 | 802.1 | 279.1 | 121.7 |
| eth_getBalance | 367.2 | 7.9 | 4.4 | 0.1 |
| eth_getTransactionByHash(latest) | 269.8 | 0.7 | 1.5 | 0.7 |
| eth_getTransactionByHash(historic) | - | 0.0 | 3.0 | 0.0 |
| eth_getTransactionReceipt(latest) | 941.6 | 672.5 | 2.6 | 1.0 |
| eth_getTransactionReceipt(historic) | - | 0.0 | 3.9 | 0.0 |

**Note:** So, while a complete block is often smaller than the corresponding JSON-RPC data (because colibri.stateless uses the binary SSZ-encoded execution payload from the beacon chain and extracts it directly), other methods such as eth_call require additional Merkle proofs for every accessed storage value before the EVM execution can be verified.

## Sync Time

The first request takes a bit longer since it needs to fetch and verify the pubkeys of the sync_committee.

Initial sync (cold cache): **285 ms**

