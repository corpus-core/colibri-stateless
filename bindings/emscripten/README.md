

<img src="c4_logo.png" alt="C4 Logo" width="300"/>

# C4 (corpus core colibri client)

![ETH2.0_Spec_Version 1.4.0](https://img.shields.io/badge/ETH2.0_Spec_Version-1.4.0-2e86c1.svg)

The colibri client is a stateless and trustless ethereum client, which is optimized for the mobile apps or embedded devices, because it does not hols the state, but verifies on demand. 

## Installation

```sh
npm install @corpus-core/colibri-stateless
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
    const client = new Colibri();

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
    const client = new Colibri();

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
    const client = new Colibri();

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
    const colibriClient = new Colibri();

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



## Building

In order to build the Javascript bindings from source, you need to have [emscripten installed](https://emscripten.org/docs/getting_started/downloads.html).

*The Colibri JS-Binding has been tested with Version 4.0.3. Using latest or other versions may result in unexpected issues. For example Version 4.0.7 is not working. So make sure you select the right version when installing!*

```sh
git clone https://github.com/corpus-core/colibri-stateless.git && cd colibri-stateless
mkdir build && cd build
emcmake cmake -DWASM=true -DCURL=false ..
make -j
```

Of course, building your own version allows you to use any of the [cmake flags options](../building/cmake-colibri-lib.md). So you can choose which chains or which Proofs schouls be supported allowing a very small wasm build.

like 
```sh
emcmake cmake -DWASM=true -DCURL=false -DETH_ACCOUNT=1 -DETH_CALL=0 -DETH_LOGS=0 -DETH_BLOCK=0 -DETH_TX=0 -DETH_RECEIPT=0 -DETH_UTIL=0 ..
```

this would turn off all code which is not needed dramticly decreasing the size, if all you want to do is verify accountProofs (like eth_getBalance).

After calling `make`, The js-module will be in the `build/emscripten` folder.


## Concept

The idea behind C4 is to create a ultra light client or better verifier which can be used in Websites, Mobile applications, but especially in embedded systems. The Proofer is a library which can used within you mobile app or in the backend to create Proof that the given data is valid. The Verifier is a library which can be used within the embedded system to verify this Proof.

The verifier itself is almost stateless and only needs to store the state of the sync committee, which changes every 27h. But with the latest sync committee the verifier is able to verify any proof with the signatures matching the previously verified public keys of the sync committee.
This allows independent Verification and security on any devices without the need to process every blockheader (as light clients usually would do).

More Details can be found on [github](https://github.com/corpus-core/c4)

## License

MIT