: Bindings

:: JavaScript/TypeScript

The JS-Bindings uses emscripten to create a webassembly which can easily be packed even using webpack or in a nodejs env.

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

{% tabs %}
{% tab title="EthersJs 6.x" %}
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
{% endtab %}
{% tab title="EthersJs 5.x" %}
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
{% endtab %}
{% tab title="Web3.js" %}
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
{% endtab %}

{% tab title="Viem" %}
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
{% endtab %}
{% endtabs %}

## Secure Transaction Verification

Colibri provides built-in protection against NPM supply-chain attacks and transaction manipulation through its transaction verification feature. When enabled, all `eth_sendTransaction` calls are automatically verified before being broadcast to the network.

### How it works

1. **Sign via Fallback Provider**: Transaction is signed using your configured fallback provider (e.g., MetaMask)
2. **Decode & Verify**: The signed transaction is decoded and compared with the original parameters
3. **Secure Broadcast**: Only if verification passes, the transaction is sent to the network

### Example

```javascript
import { BrowserProvider } from "ethers";
import Colibri from "@corpus-core/colibri-stateless";

// 🛡️ Secure transactions with built-in verification
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

console.log("✅ Verified transaction:", tx.hash);
```

This feature protects against:
- Malicious NPM packages modifying transaction parameters
- Browser extensions tampering with transactions
- Supply-chain attacks targeting transaction data

## Building proofs in you app.

If you don't want to use a remote Service building the proofs, you can also use Colibri directly to build the proof or to verify. A Proof is juzst a UInt8Array or just bytes. You write it in a file or package it in your application and verify whenever it is needed:

```js
import Colibri from "@corpus-core/colibri-stateless";

async function main() {
    const method = "eth_getTransactionByHash";
    const args = ['0x2af8e0202a3d4887781b4da03e6238f49f3a176835bc8c98525768d43af4aa24'];


    // Initialize the client with the default configuration and RPCs
    const client = new Colibri({prover:['https://mainnet.colibri-proof.tech']});

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
     new Colibri({ chainId: 'gnosis'})
     ```
- `beacon_apis` - urls for the beacon apis    
    An array of endpoints for accessing the beacon chain using the official [Eth Beacon Node API](https://ethereum.github.io/beacon-APIs/). The Array may contain more than one url, and if one API is not responding the next URL will work as fallback. This beacon API is currently used eitehr when building proofs directly or even if you are using a remote prover, the LightClientUpdates (every 27h) will be fetched directly from the beacon API.   
     ```js
     new Colibri({ beacon_apis: [ 'https://lodestar-mainnet.chainsafe.io' ]})
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
    - `"hybrid"` -- The consensus-layer proof (BlockHeaderProof) comes from the Colibri server, while execution-layer data (account proofs, storage, etc.) is fetched directly from the RPC provider. Best balance of performance and scalability -- the Colibri server only serves lightweight, cacheable header proofs while the heavy RPC load goes to your existing provider.
    - `"proxy"` -- Like remote, but the client sends its own RPC and Beacon API URLs to the prover server. The server uses these endpoints instead of its own. Useful when the client has access to private or premium RPC providers.
    - `"light_client"` -- Like hybrid, with additional background polling of block headers to keep the cache warm. Call `startLightClient()` / `stopLightClient()` to control polling. Default interval: 12000ms. By default only the compact `eth_getBlockHeader` is fetched; pass `fullBlock: true` to fetch the full block (useful when many `eth_getTransactionByHash` / `eth_getTransactionReceipt` calls follow).
    ```js
    // Explicit hybrid mode: header proofs from Colibri, execution data from RPC provider
    new Colibri({
      prover: ["https://mainnet.colibri-proof.tech"],
      rpcs: ["https://eth-mainnet.g.alchemy.com/v2/<APIKEY>"],
      prover_mode: "hybrid"
    })

    // Light client mode with background header polling
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

    Example (one signer):
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
- `checkpointz` - urls for checkpoint servers (Checkpointz or Beacon API)    
    An array of server endpoints for fetching finalized checkpoint data and weak subjectivity validation. Supports both dedicated Checkpointz servers and standard Beacon API nodes, as the verifier uses the Beacon-API-compatible endpoint `/eth/v1/beacon/states/head/finality_checkpoints`. These servers provide finalized beacon block roots that the verifier uses for secure initialization and periodic validation. The verifier automatically queries these servers when no trusted checkpoint is provided or when validating long sync gaps. Multiple URLs enable automatic fallback for resilience. Defaults to public Checkpointz servers for mainnet, but you can also use your own Beacon node for maximum trust.
    ```js
    // Using public Checkpointz servers (default for mainnet)
    new Colibri({ checkpointz: [ 'https://sync-mainnet.beaconcha.in', 'https://beaconstate.info', 'https://sync.invis.tools', 'https://beaconstate.ethstaker.cc' ]})
    
    // Or using your own Beacon node for maximum trust
    new Colibri({ checkpointz: [ 'http://localhost:5052' ]})
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
- `privacy_mode` - **PAP (Pragmatic Adaptive Privacy)** mode. Reduces intent leakage towards RPC/prover by using cached data when available and verifying afterwards. Allowed values: `"none"` (default), `"basic"`. With `"basic"`, the verifier sets the PAP flag so that method-type and verification can use cached storage for optimistic execution (e.g. for `eth_call`); method type may depend on params.
    ```js
    new Colibri({ privacy_mode: "basic" })
    ```
- `skip_wsp_check` - if `true`, the verifier skips the **Weak Subjectivity Period (WSP) check** for prover-supplied or self-fetched sync committee data, setting `VERIFY_FLAG_SKIP_WSP_CHECK` (bit `1 << 7`). The WSP check anchors the highest finalized header against the configured `checkpointz` endpoint whenever a sync crosses the WSP (typically ~2 to 4 months on Ethereum mainnet); for ZK sync data the verifier prefers `checkpoint_witness_keys` + matching signatures when available, otherwise falls back to `checkpointz`. **SECURITY:** only enable when another trust anchor (witness signatures, hard-coded checkpoint, signed package) is in place; disabling raises the risk of long-range attacks. Default: `false`. See the [threat model -- long range attacks](https://corpus-core.gitbook.io/specification-colibri-stateless/specifications/ethereum/threat-model) for background.
    ```js
    new Colibri({ skip_wsp_check: true })
    ```

- `max_latest_age_seconds` - upper bound (in seconds) on the age of a proof whose request uses the **`"latest"`** block tag. Without this guard a proof for an old `latest` block remains cryptographically valid forever and could be replayed as "current" months later. The binding reads `Date.now()` and forwards `now - max_latest_age_seconds` to the verifier, which rejects stale proofs with `"proof for latest too old"`. Currently active for `eth_call`, `eth_estimateGas`, and `colibri_simulateTransaction`. Set to `0` to disable the check (useful for legacy proof formats that do not embed a block context). Default: `60` (≈ 5 Ethereum slots). The check also applies in PAP mode, where the call proof arrives via `colibri_proofCall` (same proof structure as a direct `eth_call`); this requires a prover that embeds the block context (≥ 1.1.15). Against an older PAP proof without a block timestamp the check fails closed with `"cannot verify freshness of latest block without block context"` -- set `max_latest_age_seconds: 0` to opt out. **Caveat:** the gate fires only on `"latest"` (not `"safe"`/`"finalized"`), and if the host wallclock is below `max_latest_age_seconds` (embedded boards before NTP sync, sandboxed environments) the lower bound clamps to `0` and the check is silently disabled; ensure your runtime has a synced clock or set `max_latest_age_seconds: 0` explicitly to acknowledge this state.
    ```js
    new Colibri({ max_latest_age_seconds: 30 })
    ```

- `oblivious_nodes` - TEE RPC endpoints for private `eth_getProof` (default: empty). Routes `eth_getProof` only to these URLs; sets `VERIFY_FLAG_OBLIVIOUS` and **PAP** automatically. For full `eth_call` privacy, combine with `privacy_mode: "basic"` and `prover_mode: "hybrid"`:
    ```js
    // https://rpc.safe-node.com/ requires an API key for testing
    new Colibri({
      privacy_mode: "basic",
      prover_mode: "hybrid",
      oblivious_nodes: ["https://rpc.safe-node.com/"],
    })
    ```
    **Why hybrid:** only the block proof is fetched from the prover; account/storage data is loaded from RPC or oblivious node and verified locally (remote mode would download the full call proof from the server). **Why PAP:** avoids `eth_createAccessList` on the prover (intent leakage); storage slots are resolved optimistically in the local EVM so only `eth_getProof` RPCs are exposed externally.

    How oblivious RPC nodes work (TEE, Oblivious RAM): [Oblivious Labs](https://www.obliviouslabs.com/).

- `fetch` - custom fetch function for all HTTP requests    
    Provide a custom `fetch` implementation to route all network traffic through Tor, a SOCKS proxy, or any other transport layer. The function must match the signature of `globalThis.fetch`. When not set, the standard `fetch` is used.
    ```js
    // Browser: route through Tor via Arti WASM (bootstrap runs in background)
    import { createBrowserFetch } from '@corpus-core/colibri-tor/browser';
    const torFetch = createBrowserFetch();
    new Colibri({ fetch: torFetch })
    ```
    ```js
    // Node.js: route through a locally running Tor SOCKS5 proxy
    import { createSocksFetch } from '@corpus-core/colibri-tor/node';
    const torFetch = await createSocksFetch({ socksPort: 9050 });
    new Colibri({ fetch: torFetch })
    ```

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

### CMake Presets (Recommended)


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

### emcmake

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

