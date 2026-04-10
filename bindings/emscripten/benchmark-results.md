## End-to-End Performance

Measured using the WASM client against `mainnet1.colibri-proof.tech`.  
3 blocks, 5 runs per block. Best-case (min) latency shown.

### Latency (best-case, ms)

| Method | local | hybrid | lightclient | proxy | remote | unverified |
|--------|------|------|------|------|------|------|
| eth_blockNumber | 119 | 0 | 0 | 56 | 58 | 19 |
| eth_getBlockByNumber(false) | 127 | 9 | 5 | 106 | 114 | 22 |
| eth_getBlockByNumber(true) | 348 | 559 | 189 | 843 | 467 | 41 |
| eth_call | 224 | 60 | 52 | 74 | 59 | 18 |
| eth_getLogs | 309 | 112 | 113 | 157 | 149 | 20 |
| eth_getBalance | 176 | 22 | 23 | 58 | 58 | 20 |
| eth_getTransactionByHash(latest) | 199 | 40 | 44 | 65 | 61 | 19 |
| eth_getTransactionByHash(historic) | - | 18 | 18 | 163 | 133 | 19 |
| eth_getTransactionReceipt(latest) | 226 | 98 | 98 | 80 | 92 | 20 |
| eth_getTransactionReceipt(historic) | - | 18 | 19 | 223 | 249 | 19 |

### Transfer Size (avg, kB)

| Method | local | hybrid | lightclient | proxy | remote | unverified |
|--------|------|------|------|------|------|------|
| eth_blockNumber | 369.2 | 0.0 | 0.0 | 0.7 | 0.7 | 0.0 |
| eth_getBlockByNumber(false) | 370.3 | 26.0 | 81.3 | 166.0 | 174.4 | 24.9 |
| eth_getBlockByNumber(true) | 370.3 | 0.0 | 0.0 | 166.0 | 174.4 | 529.7 |
| eth_call | 459.0 | 95.0 | 97.5 | 27.7 | 27.7 | 0.1 |
| eth_getLogs | 775.4 | 441.9 | 437.9 | 136.1 | 136.1 | 39.1 |
| eth_getBalance | 362.3 | 7.9 | 7.9 | 4.4 | 4.4 | 0.1 |
| eth_getTransactionByHash(latest) | 339.6 | 2.2 | 2.2 | 2.2 | 2.2 | 2.2 |
| eth_getTransactionByHash(historic) | - | 0.0 | 0.0 | 4.8 | 4.8 | 0.0 |
| eth_getTransactionReceipt(latest) | 738.4 | 401.0 | 401.0 | 6.4 | 6.4 | 5.8 |
| eth_getTransactionReceipt(historic) | - | 0.0 | 0.0 | 35.8 | 35.8 | 0.0 |

### Sync Time

Initial sync (cold cache): **321 ms**

### PAP Impact on Latency (best / worst overhead, ms)

| Method | local | hybrid | lightclient | proxy | remote |
|--------|------|------|------|------|------|
| eth_blockNumber | +35 / +348 | +0 / +0 | +0 / +0 | -2 / +164 | -5 / +156 |
| eth_getBlockByNumber(false) | +44 / +217 | -6 / +177 | +7 / +304 | -4 / +153 | -18 / +172 |
| eth_getBlockByNumber(true) | +288 / +985 | -457 / -301 | +515 / +981 | -15 / +277 | +32 / +1608 |
| eth_call | +56 / +372 | +62 / +161 | +70 / +269 | -4 / +229 | +1 / +538 |
| eth_getLogs | -12 / +240 | +5 / +120 | +14 / +107 | +94 / +303 | +89 / +310 |
| eth_getBalance | +19 / +135 | -1 / +48 | -2 / +78 | -2 / +110 | +2 / +424 |
| eth_getTransactionByHash(latest) | +5 / +193 | +39 / +62 | +35 / +336 | +14 / +122 | +15 / +529 |
| eth_getTransactionByHash(historic) | - | +167 / +295 | +180 / +358 | +20 / +216 | +50 / +280 |
| eth_getTransactionReceipt(latest) | +6 / +128 | -20 / -9 | -19 / +85 | +52 / +163 | +40 / +365 |
| eth_getTransactionReceipt(historic) | - | +117 / +533 | +115 / +219 | +37 / +372 | +42 / +506 |

