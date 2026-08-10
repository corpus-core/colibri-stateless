

<img src="c4_logo.png" alt="Colibri Logo" width="300"/>

# Colibri Stateless — JavaScript / TypeScript

**Verify Ethereum RPC data cryptographically — without running a full node.**

![ETH2.0 Spec Version 1.4.0](https://img.shields.io/badge/ETH2.0_Spec_Version-1.4.0-2e86c1.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)
[![npm](https://img.shields.io/npm/v/@corpus-core/colibri-stateless.svg)](https://www.npmjs.com/package/@corpus-core/colibri-stateless)

Colibri Stateless is a highly efficient prover/verifier for Ethereum (with upcoming support for Layer-2s such as OP-Stack). This package runs the C core as WebAssembly in Node.js and the browser, and implements the [EIP-1193](https://eips.ethereum.org/EIPS/eip-1193) provider interface — so it drops straight into ethers, web3.js, or viem while cryptographically verifying every response.

[**Website**](https://www.corpuscore.tech/colibri) · [**Docs**](https://corpus-core.gitbook.io/specification-colibri-stateless/developer-guide/bindings/javascript-typescript) · [**Whitepaper**](https://corpus-core.gitbook.io/whitepaper-colibri-stateless) · [**Privacy (PAP)**](https://corpus-core.gitbook.io/pap-colibri-stateless)

## Why Colibri?

- **Stateless** — verification needs nothing but the proof and the sync committee it is checked against. The sync committee is cached locally so it does not have to travel with every request, but it works just as well with an empty cache or none at all. No persistent state, no block-by-block header processing, no full node.
- **Cryptographically verified RPC** — the prover creates proofs for the validity of RPC responses; the verifier checks them against BLS signatures.
- **Offline verification** — a proof is fully self-contained and can be verified without any network connection, thanks to zk-proofs for the sync committee and signed checkpoints.
- **On-demand, not always-on** — Colibri only does work when you actually make a request. It does not continuously sync in the background, so it never burns bandwidth, CPU, or battery while your app is idle.
- **Verifies historical data (older than ~27h / 8192 blocks)** — using `historical_summaries` Merkle proofs from the beacon state, Colibri cleanly verifies old transactions and receipts where other light clients simply fail.
- **`eth_getLogs` completeness proofs** — cryptographic guarantee that for a requested block range no matching event was omitted (opt-in via `logs_completeness`).
- **Fully verified local transaction simulation** — simulate a transaction against verified state *before signing*, so users can be shown exactly what a transaction will do.
- **Tiny & fast** — most requests are barely slower than a plain RPC call — see the [benchmarks](https://corpus-core.gitbook.io/specification-colibri-stateless/specifications/ethereum/benchmark).
- **Privacy-aware** — Pragmatic Adaptive Privacy (PAP) mode. See the [Privacy Whitepaper](https://corpus-core.gitbook.io/pap-colibri-stateless).

## Installation

```sh
npm install @corpus-core/colibri-stateless
```

## Import / Usage (ESM and CommonJS)

Colibri is published as a **dual package** and can be used in both ESM and CommonJS environments.

### ESM (browsers, modern bundlers, Node ESM)

```js
import Colibri, { Strategy, set_wasm_url } from "@corpus-core/colibri-stateless";

// Optional: only needed if you want to pin the WASM location explicitly
// set_wasm_url("https://example.com/c4w.wasm");

const client = new Colibri();
```

### CommonJS (e.g. Jest, older Node toolchains)

```js
const { default: Colibri, Strategy, set_wasm_url } = require("@corpus-core/colibri-stateless");

// Optional: in Node you can explicitly point to the wasm file path
// set_wasm_url(require("node:path").join(__dirname, "c4w.wasm"));

const client = new Colibri();
```

## Using Colibri as RPC Provider

The Colibri Class implements the EIP-1193 Interface, so any library supporting EIP-1193 Providers can easily use Colibri as RPCProvider. 

Right now Subscription and Filters have not been implemented, so in case you need those features, jus use a different Provider for those tasks and the verify the found logs using Colibri. But those features will be implemented in one of the next releases.

### EthersJs 6.x
```javascript
import { BrowserProvider } from "ethers";
import Colibri from "@corpus-core/colibri-stateless";

async function main() {

    // Initialize the client with the default configuration and RPCs
    const client = new Colibri({prover:['https://mainnet.colibri-proof.tech']});

    // Use Colibri client as the EIP-1193 provider for ethers (v6)
    const provider = new BrowserProvider(client);

    // Fetch the latest block using the ethers provider
    const block = await provider.getBlock('latest');
    console.log("Block fetched via ethers:", block);
}

main().catch(console.error);
```
### EthersJs 5.x
```javascript
import * as ethers from "ethers";
import Colibri from "@corpus-core/colibri-stateless";

async function main() {

    // Initialize the client with the default configuration and RPCs
    const client = new Colibri({prover:['https://mainnet.colibri-proof.tech']});

    // Use Colibri client as the EIP-1193 provider for ethers (v6)
    const provider = new ethers.providers.Web3Provider(client);

    // Fetch the latest block using the ethers provider
    const block = await provider.getBlock('latest');
    console.log("Block fetched via ethers:", block);
}

main().catch(console.error);
```

### Web3.js

```javascript
import Web3 from 'web3';
import Colibri from "@corpus-core/colibri-stateless";

async function main() {

    // Initialize the client with the default configuration and RPCs
    const client = new Colibri({prover:['https://mainnet.colibri-proof.tech']});

    // Use Colibri client as the EIP-1193 provider for web3.js
    const web3 = new Web3(client);

    // Fetch the latest block using the web3.js provider
    const block = await web3.eth.getBlock('latest');
    console.log("Block fetched via web3.js:", block);
}

main().catch(console.error);
```

### Viem

```javascript
import { createPublicClient, custom } from 'viem';
import { mainnet } from 'viem/chains';
import Colibri from "@corpus-core/colibri-stateless";

async function main() {

    // Initialize the Colibri client
    const colibriClient = new Colibri({prover:['https://mainnet.colibri-proof.tech']});

    // Create a viem Public Client using Colibri as a custom EIP-1193 transport
    const viemClient = createPublicClient({
        chain: mainnet, // Specify the chain
        transport: custom(colibriClient) // Wrap Colibri client
    });

    // Fetch the latest block using the viem client
    const block = await viemClient.getBlock({ blockTag: 'latest' });
    console.log("Block fetched via viem:", block);
}

main().catch(console.error);
```

## Secure Transaction Verification

Protect your dApp from NPM supply-chain attacks and transaction manipulation:

```javascript
import { BrowserProvider } from "ethers";
import Colibri from "@corpus-core/colibri-stateless";

// Secure transactions with built-in verification
const client = new Colibri({
    fallback_provider: window.ethereum, // MetaMask as Signer
    verifyTransactions: true            // Prevents transaction manipulation
});

const provider = new BrowserProvider(client);

// Send transaction - automatically verified before broadcast
const tx = await provider.getSigner().sendTransaction({
    to: "0x742d35cc6633C0532925a3b8D8C9C4e2F9",
    value: "0x16345785d8a0000", // 0.1 ETH
    gasLimit: "0x5208"
});

console.log("Verified transaction:", tx.hash);
```

## Building proofs in you app.

If you don't want to use a remote Service building the proofs, you can also use Colibri directly to build the proof or to verify. A Proof is juzst a UInt8Array or just bytes. You write it in a file or package it in your application and verify whenever it is needed:

```js
import Colibri from "@corpus-core/colibri-stateless";

async function main() {
    const method = "eth_getTransactionByHash";
    const args = ['0x2af8e0202a3d4887781b4da03e6238f49f3a176835bc8c98525768d43af4aa24'];


    // Initialize the client with the default configuration and RPCs
    const client = new Colibri();

    // Create a proof for the given method and arguments as UInt8Array
    const proof = await client.createProof(method, args);

    // Verify the proof against requested method and arguments
    const result = await client.verifyProof(method, args, proof);

    // the result will be the expected json
    console.log(result);
}

main().then(console.log).catch(console.error);
```

## Configuration

The constructor of the colibri client accepts a configuration-object, which may configure the client. The following parameters are accepted:

- `chainId` - the chain to be used (default is 1, whoich is mainnet).   

     ```js
     new Colibri({ chainId: 0x7})
     ```
- `beacon_apis` - urls for the beacon apis    
    An array of endpoints for accessing the beacon chain using the official [Eth Beacon Node API](https://ethereum.github.io/beacon-APIs/). The Array may contain more than one url, and if one API is not responding the next URL will work as fallback. This beacon API is currently used eitehr when building proofs directly or even if you are using a remote prover, the LightClientUpdates (every 27h) will be fetched directly from the beacon API.   
     ```js
     new Colibri({ beacon_apis: [ 'https://lodestar-mainnet.chainsafe.io' ]})
     ```
- `checkpointz` - urls for checkpoint servers (Checkpointz or Beacon API)    
    An array of server endpoints for fetching finalized checkpoint data and weak subjectivity validation. Supports both dedicated Checkpointz servers and standard Beacon API nodes, as the verifier uses the Beacon-API-compatible endpoint `/eth/v1/beacon/states/head/finality_checkpoints`. These servers provide finalized beacon block roots used for secure initialization and periodic validation. Multiple URLs enable automatic fallback. Defaults to public Checkpointz servers for mainnet, but you can also use your own Beacon node for maximum trust.
     ```js
     // Using public Checkpointz servers (default)
     new Colibri({ checkpointz: [ 'https://sync-mainnet.beaconcha.in', 'https://beaconstate.info' ]})
     
     // Or using your own Beacon node
     new Colibri({ checkpointz: [ 'http://localhost:5052' ]})
     ```
- `rpcs` - RPCs for the executionlayer    
    a array of rpc-endpoints for accessing the execution layer. If you are using the remote prover, you may not need it at all. But creating your proofs locally will require to access data from the execution layer. Having more than one rpc-url allows to use fallbacks in case one is not available.
     ```js
     new Colibri({ beacon_apis: [
        "https://nameless-sly-reel.quiknode.pro/<APIKEY>/",
        "https://eth-mainnet.g.alchemy.com/v2/<APIKEY>",
        "https://rpc.ankr.com/eth/<APIKEY>" ]})
     ```
- `prover` - urls for remote prover
    an array of endpoints for remote prover. This allows to generate the proof in the backend, where caches can speed up the process.
    ```js
    new Colibri({ prover: ["https://mainnet.colibri-proof.tech" ]})
    ```
- `prover_mode` - proof generation mode (default: `"remote"` if prover URLs configured, otherwise `"local"`)
    Controls how proofs are built and verified. Five modes are available:
    - `"local"` -- Proofs are built entirely on the client. Requires access to a Beacon API and execution layer RPC. Fully trustless, but slower and needs more infrastructure.
    - `"remote"` -- Proofs are fetched from a remote Colibri prover server. Fastest option but relies on the prover server for proof generation. The verifier still cryptographically checks every proof.
    - `"hybrid"` -- The consensus-layer proof (BlockHeaderProof) comes from the Colibri server, while execution-layer data (account proofs, storage, etc.) is fetched directly from the RPC provider. Best balance of performance and scalability -- the Colibri server only serves lightweight, cacheable header proofs while the heavy RPC load goes to your existing provider. The delegated `eth_getBlockHeader` / `eth_getBlockByNumber` requests are sent as **cache-friendly GET requests** (`GET /proof/<method>/<block>/<version>/<zk>/<c4>`) with a `Cache-Control` freshness bound, so a CDN in front of the prover can absorb repeated requests for the same head (only ~one new block every 12s).
    - `"proxy"` -- Like remote, but the client sends its own RPC and Beacon API URLs to the prover server. The server uses these endpoints instead of its own. Useful when the client has access to private or premium RPC providers.
    - `"light_client"` -- Like hybrid, with additional background polling of block headers to keep the cache warm. Call `startLightClient()` / `stopLightClient()` to control polling. Default interval: 12000ms. By default only the compact `eth_getBlockHeader` is fetched; pass `fullBlock: true` to fetch the full block (useful when many `eth_getTransactionByHash` / `eth_getTransactionReceipt` calls follow).
    ```js
    // Explicit hybrid mode
    new Colibri({
      prover: ["https://mainnet.colibri-proof.tech"],
      rpcs: ["https://eth-mainnet.g.alchemy.com/v2/<APIKEY>"],
      prover_mode: "hybrid"
    })

    // Light client mode
    const client = new Colibri({
      prover: ["https://mainnet.colibri-proof.tech"],
      rpcs: ["https://eth-mainnet.g.alchemy.com/v2/<APIKEY>"],
      prover_mode: "light_client"
    });
    client.startLightClient();              // polls eth_getBlockHeader every 12s
    client.startLightClient(12000, true);   // or fetch the full block
    ```
- `zk_proof` - use remote ZK sync proof for bootstrap (default: `false`)
    If `true`, the verifier will bootstrap the initial sync committee using the ZK proof (`ZKSyncData`) provided by the remote prover, instead of initializing via `checkpointz` / trusted checkpoints.
    ```js
    new Colibri({ prover: ["https://mainnet.colibri-proof.tech"], zk_proof: true })
    ```
- `checkpoint_witness_keys` - optional checkpoint signer addresses when using `zk_proof` (default: `null`)
    A list of Ethereum addresses (20 bytes each). The current format is a single hex string where multiple addresses are **concatenated** (no separator).

    Example (one signer, corpus-core):
    ```js
    new Colibri({
      prover: ["https://mainnet.colibri-proof.tech"],
      zk_proof: true,
      checkpoint_witness_keys: "0x07f50c1d17cb84a656692ddfd577c09756cb305b"
    })
    ```

    Example (two signers concatenated):
    ```js
    new Colibri({
      zk_proof: true,
      checkpoint_witness_keys: "0x<addr1_40hex><addr2_40hex>"
    })
    ```
- `trusted_checkpoint` - optional beacon block hash used as trusted anchor    
    This single blockhash will be used as anchor for fetching the keys for the sync committee. So instead of starting with the genesis you can define a starting block, where you know the blockhash. If no trusted checkpoint is set, the verifier will automatically fetch the latest finalized checkpoint from a Checkpointz server, making initialization secure and convenient. Providing an explicit trusted checkpoint is recommended for maximum security control but is no longer required.
    ```js
    new Colibri({ trusted_checkpoint: "0x4232db57354ddacec40adda0a502f7732ede19ba0687482a1e15ad20e5e7d1e7" })
    ```
- `cache` - cache impl for rpc-requests    
    you can provide your own implementation to cache JSON-RPC requests. those function will be used before a request is send, also allowing mock handlers to cache responses for tests.
    ```js
    new Colibri({ cache: {
        cacheable(req: DataRequest) { 
            return req.payload && req.payload.method!='eth_blockNumber' 
        },
        get(req: DataRequest) {
            try {
                return fs.readFileSync(`${cache_dir}/${req.url}`);
            }
            catch (e) {
                return null
            }
        },
        set(req: DataRequest, data: Uint8Array) {
            fs.writeFileSync(`${cache_dir}/${req.url}`, data);
        }
    }})
    ```
- `debug` - if true you will see debug output on the console     
    ```js
    new Colibri({ debug:  true})
    ```
- `include_code`- if true the code of the contracts will be included when creating proofs. this is only  relevant when creating your own proofs for eth_call. (default: false)
    ```js
    new Colibri({ include_code:  true})
    ```
- `use_accesslist` - if `true` (default), local `eth_call` / `eth_estimateGas` / `colibri_simulateTransaction` proofs use `eth_createAccessList` to discover touched accounts and storage. Set to `false` only to opt into the legacy `debug_traceCall` (prestateTracer) path (`C4_PROVER_FLAG_USE_DEBUG_TRACE`); that API is not available on every RPC provider, while contract code is usually cached anyway. Irrelevant when proofs are fetched from a remote prover that already chose its own path.
    ```js
    new Colibri({ use_accesslist: true })   // default
    new Colibri({ use_accesslist: false })  // legacy debug_traceCall
    ```
- `logs_completeness` - if true, `eth_getLogs` produces (prover) and requires (verifier) a **completeness proof** over the requested block range `[fromBlock, toBlock]`. It proves that no matching log was omitted, not just that the returned logs are valid. This sets the prover flag (`1 << 12`) and the verifier flag (`1 << 9`) and requires a prover that supports it. The range end (`toBlock`) may be a pinned block hash/number or `"latest"`; `"safe"`/`"finalized"` are not supported yet. (default: false, tracks issue #128)
    ```js
    new Colibri({ logs_completeness: true })
    ```
- `privacy_mode` - **PAP (Pragmatic Adaptive Privacy)** mode: `"none"` (default) or `"basic"`. With `"basic"`, the verifier may use cached storage for optimistic execution and verify afterwards; method type can depend on params. *This feature is still experimental!*
    ```js
    new Colibri({ privacy_mode: "basic" })
    ```

- `oblivious_nodes` - TEE RPC endpoints for private `eth_getProof` (default: empty). Sets `VERIFY_FLAG_OBLIVIOUS` and **PAP** automatically. For a privacy-preserving `eth_call`, also use `privacy_mode: "basic"` (or rely on auto-PAP when oblivious is set), `prover_mode: "hybrid"`, and oblivious nodes — see below.
    ```js
    // Privacy-preserving eth_call (https://rpc.safe-node.com/ needs an API key for testing)
    new Colibri({
      privacy_mode: "basic",
      prover_mode: "hybrid",
      oblivious_nodes: ["https://rpc.safe-node.com/"],
    })
    ```
    **Why:** `hybrid` fetches only the block proof from the prover; storage values come from RPC/oblivious node and are verified locally. `basic` (PAP) avoids `eth_createAccessList` on the prover (which would leak intent); the EVM discovers storage optimistically so only `eth_getProof` requests leave the client.

    TEE/ORAM details: [Oblivious Labs](https://www.obliviouslabs.com/).

- `verify`- a function to decide which request should be verified and which should be fetched from the default RPC-Provider. It allows you to speed up performance for requests which are not critical.
    ```js
    new Colibri({ verify:  (method, args) => method != 'eth_blockNumber' })
    ```

- `proofStrategy`- a strategy function used to determine how to handle proofs. Currently there are 3 default-implementations.

    -  `Strategy.VerifiedOnly` - throws an exception if verifaction fails or a non verifieable function is called.
    -  `Strategy.VerifyIfPossible` - Verifies only verifiable rpc methods and uses the fallbackhandler or rpcs if the method is not verifiable, but throws an exception if verifaction fails.
    -  `Strategy.WarningWithFallback` - Always use the defaultprovider or rpcs to fetch the response and in parallel verifiy the response if possible. If the Verification fails, the warningHandler is called ( which still could throw an exception ). If it fails the response from the rpc-provider is used.


    ```js
    new Colibri({ proofStrategy: Strategy.VerifyIfPossible })
    ```



- `warningHandler`- a function to be called in case the warning-strategy is used and a verification-error happens. If not set, the default will simply use console.warn to log the error.
    ```js
    new Colibri({ warningHandler:  (req, error) => console.warn(`Verification Error: ${error}`) })
    ```


- `fallback_provider`- a EIP 1193 Provider used as fallback for all requests which are not verifieable, like eth_sendTransaction. Also used for signing transactions when `verifyTransactions` is enabled.
    ```js
    new Colibri({ fallback_provider: window.ethereum  })
    ```

- `verifyTransactions`- if true, all eth_sendTransaction calls will be verified before broadcast to prevent transaction manipulation attacks. Requires `fallback_provider` to be set. (default: false)
    ```js
    new Colibri({ 
        fallback_provider: window.ethereum,
        verifyTransactions: true 
    })
    ```



## Building

In order to build the Javascript bindings from source, you need to have [emscripten installed](https://emscripten.org/docs/getting_started/downloads.html) and the `EMSDK` environment variable pointing to its installation directory.

*The Colibri JS-Binding has been tested with Version 4.0.3. Using latest or other versions may result in unexpected issues. For example Version 4.0.7 is not working. So make sure you select the right version when installing!*

**Recommended Method: Using CMake Presets**

This project includes a `CMakePresets.json` file for easier configuration.

1.  **Set Environment Variable:** Ensure the `EMSDK` environment variable points to your Emscripten SDK directory.
    ```sh
    export EMSDK=/path/to/your/emsdk
    ```
2.  **Configure using Preset:** Use the `wasm` preset. 
    *   **In VS Code/Cursor:** Select the `[wasm]` configure preset via the status bar or command palette (`CMake: Select Configure Preset`).
    *   **On the Command Line:**
        ```sh
        # Configure (from the project root)
        cmake --preset wasm -S . 
        # The binary directory (e.g., build/wasm) is defined in the preset
        ```
3.  **Build:**
    *   **In VS Code/Cursor:** Use the build button or the command palette (`CMake: Build`). Make sure the `[wasm]` build preset is selected.
    *   **On the Command Line:**
        ```sh
        # Build (using the build directory from the preset)
        cmake --build build/wasm -j
        ```

This preset automatically sets `-DWASM=true`, `-DCURL=false`, and the correct toolchain file based on your `EMSDK` variable. You can create custom presets in `CMakeUserPresets.json` if you need different CMake flags (e.g., `-DETH_ACCOUNT=1`).

**Alternative Method: Manual `emcmake`**

If you prefer not to use presets or your environment doesn't support them well:

1.  **Set Environment Variable:** Ensure `EMSDK` is set and the Emscripten environment is active (e.g., via `source ./emsdk_env.sh`).
2.  **Configure and Build:**
    ```sh
    git clone https://github.com/corpus-core/colibri-stateless.git && cd colibri-stateless
    mkdir build/wasm-manual && cd build/wasm-manual # Use a dedicated build dir
    # Ensure EMSDK is set correctly before running emcmake
    emcmake cmake -DWASM=true -DCURL=false <other_flags> ../..
    make -j
    ```
    Replace `<other_flags>` with any additional CMake options you need (like `-DETH_ACCOUNT=1`).

After a successful build (using either method), the JS/WASM module will be in the configured build directory's `emscripten` subfolder (e.g., `build/wasm/emscripten`).


## Debugging WASM

When tracking down bugs in the C core running as WASM, there are two approaches depending on the level of detail you need.

### Source-level C debugging in the browser (recommended)

The `wasm-debug` CMake preset produces a WASM build with full [DWARF](https://yurydelendik.github.io/webassembly-dwarf/) debug info. Combined with the Chrome extension **C/C++ DevTools Support (DWARF)**, this allows setting breakpoints in C source files, inspecting variables and structs, and stepping through C code directly in Chrome DevTools.

1. Install the Chrome extension [C/C++ DevTools Support (DWARF)](https://chromewebstore.google.com/detail/cc++-devtools-support-dwa/pdcpmagijalfljmkmjngeonclgbbannb).

2. Build with the debug preset:
   ```sh
   cmake --preset wasm-debug
   cmake --build build/wasm-debug
   ```

3. Serve the project root (so both build output and source files are accessible):
   ```sh
   python3 -m http.server 8080
   ```

4. Open the debug test harness in Chrome:
   ```
   http://localhost:8080/bindings/emscripten/test/debug.html
   ```

5. Open DevTools (F12), go to the **Sources** tab. After the WASM module loads, your C source files appear in the file tree. Set breakpoints, select a test case, and click Run.

> **Note:** The DWARF extension only works in browser tabs, not in Node.js inspect sessions. For C-level debugging, always use the browser approach.

The debug build uses `-O1` instead of `-O0` to keep crypto operations (BLS, SHA256) at a reasonable speed while preserving most debug information. If you need full variable visibility at the cost of performance, change `-O1` to `-O0` in `bindings/emscripten/CMakeLists.txt`.

### Node.js test debugging (JS-level)

For debugging the JavaScript/TypeScript layer or seeing C function names in stack traces without full source mapping:

```sh
cd bindings/emscripten
node --inspect-brk --test test/rpc.test.mjs
```

Then open `chrome://inspect` in Chrome and click "inspect" on the Node.js target. You can set breakpoints in the JS/TS files and see readable C function names in call stacks (via `--profiling-funcs`), but C source stepping is not available in this mode.

There is also a VS Code / Cursor launch configuration **"WASM Node Test (inspect)"** that starts the debugger automatically.

## Concept

The idea behind Colibri is to create an ultra-light client — or rather a verifier — that can be used in websites, mobile applications, and especially embedded systems. The prover is a library used within your app or backend to create a proof that the given data is valid. The verifier is a library used on the client (or embedded system) to verify that proof.

The verifier is stateless: it only needs the current sync committee (which rotates every ~27h), and that committee can be cached, fetched on demand, or delivered with the proof. With it, the verifier can validate any proof whose signatures match the verified public keys of the sync committee — enabling independent verification on any device without processing every block header (as classic light clients do).

More details can be found in the [documentation](https://corpus-core.gitbook.io/specification-colibri-stateless) and on [GitHub](https://github.com/corpus-core/colibri-stateless).

## License

MIT — see the main [repository](https://github.com/corpus-core/colibri-stateless) for details.