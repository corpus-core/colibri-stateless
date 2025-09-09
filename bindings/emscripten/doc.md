: Bindings

:: JavaScript/TypeScript

The JS-Bindings uses emscripten to create a webassembly which can easily be packed even using webpack or in a nodejs env.

## Installation

```sh
npm install @corpus-core/colibri-stateless
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
    const client = new Colibri({proofer:['https://mainnet.colibri-proof.tech']});

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
    const client = new Colibri({proofer:['https://mainnet.colibri-proof.tech']});

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
    const client = new Colibri({proofer:['https://mainnet.colibri-proof.tech']});

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
    const colibriClient = new Colibri({proofer:['https://mainnet.colibri-proof.tech']});

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
    const client = new Colibri({proofer:['https://mainnet.colibri-proof.tech']});

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
    An array of endpoints for accessing the beacon chain using the official [Eth Beacon Node API](https://ethereum.github.io/beacon-APIs/). The Array may contain more than one url, and if one API is not responding the next URL will work as fallback. This beacon API is currently used eitehr when building proofs directly or even if you are using a remote proofer, the LightClientUpdates (every 27h) will be fetched directly from the beacon API.   
     ```js
     new Colibri({ beacon_apis: [ 'https://lodestar-mainnet.chainsafe.io' ]})
     ```
- `rpcs` - RPCs for the executionlayer    
    a array of rpc-endpoints for accessing the execution layer. If you are using the remote proofer, you may not need it at all. But creating your proofs locally will require to access data from the execution layer. Having more than one rpc-url allows to use fallbacks in case one is not available.
     ```js
     new Colibri({ beacon_apis: [
        "https://nameless-sly-reel.quiknode.pro/<APIKEY>/",
        "https://eth-mainnet.g.alchemy.com/v2/<APIKEY>",
        "https://rpc.ankr.com/eth/<APIKEY>" ]})
     ```
- `proofer` - urls for remove proofer
    a array of endpoints for remote proofer. This allows to generate the proof in the backend, where caches can speed up the process.
    ```js
    new Colibri({ proofer: ["https://mainnet.colibri-proof.tech" ]})
    ```
- `trusted_block_hashes` - beacon block hashes used as trusted anchor    
    Thise array of blockhashes will be used as anchor for fetching the keys for the sync committee. So instead of starting with the genesis you can define a starting block, where you know the blockhash. If not trusted blockhash is set the beacon-api will be used to fetch one, but defining it is safer.
    ```js
    new Colibri({ trusted_block_hashes: [ "0x4232db57354ddacec40adda0a502f7732ede19ba0687482a1e15ad20e5e7d1e7" ]})
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

