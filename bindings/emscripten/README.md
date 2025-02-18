

<img src="c4_logo.png" alt="C4 Logo" width="300"/>

# C4 (corpus core colibri client)

![ETH2.0_Spec_Version 1.4.0](https://img.shields.io/badge/ETH2.0_Spec_Version-1.4.0-2e86c1.svg)
[![CI on multiple platforms](https://github.com/corpus-core/c4/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/corpus-core/c4/actions/workflows/cmake-multi-platform.yml)

The colibri client is a stateless and trustless ethereum client, which is optimized for the mobile apps or embedded devices, because it does not hols the state, but verifies on demand. 

## Installation

```sh
npm install @corpus-core/colibri-stateless
```

## Usage

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

    console.log(result);
}

main().then(console.log).catch(console.error);

```

## Building

In order to build the Javascript bindings, you need to have [emscripten installed](https://emscripten.org/docs/getting_started/downloads.html). 

```sh
git clone https://github.com/corpus-core/c4.git
cd c4
mkdir build
cd build
emcmake cmake -DWASM=true -DCURL=false ..
make
```
The js-module will be in the `build/emscripten` folder.

## Concept

The idea behind C4 is to create a ultra light client or better verifier which can be used in Websites, Mobile applications, but especially in embedded systems. The Proofer is a library which can used within you mobile app or in the backend to create Proof that the given data is valid. The Verifier is a library which can be used within the embedded system to verify this Proof.

The verifier itself is almost stateless and only needs to store the state of the sync committee, which changes every 27h. But with the latest sync committee the verifier is able to verify any proof with the signatures matching the previously verified public keys of the sync committee.
This allows independent Verification and security on any devices without the need to process every blockheader (as light clients usually would do).

More Details can be found on [github](https://github.com/corpus-core/c4)

## License

MIT