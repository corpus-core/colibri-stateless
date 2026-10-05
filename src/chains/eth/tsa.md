: Ethereum

:: Transaction Security Assistant

Before you sign a contract, you read it. More importantly, you make sure you understand what will happen once your signature is on it: what you give, what you get, and what you are now obligated to.

On Ethereum, we do the exact opposite every day. A transaction is just a sequence of bytes. In most cases a dApp prepares those bytes, the wallet shows a contract address, a hex blob and maybe a function name, and the user clicks "Confirm". The user can only hope that what they sign actually matches what they intended. This is blind signing, and it is one of the largest attack surfaces in the ecosystem.

The **Transaction Security Assistant (TSA)** is our attempt to change that. It simulates a transaction locally on verified state, decodes everything that happens, and lets a small language model running in your browser explain it in plain language, before you sign.

## Blind signing is not a theoretical risk

Two incidents show how expensive the gap between "what I see" and "what I sign" can be.

**Bybit, February 2025 (more than $1.4 billion).** On 21 February 2025, attackers drained over 401,000 ETH from Bybit's Safe multisig cold wallet, the largest crypto theft on record. They had injected malicious JavaScript into the Safe{Wallet} web interface via a compromised developer machine. The code only activated for Bybit's signers: the UI showed a routine transfer, while the payload sent to the hardware wallets was a `delegatecall` to an attacker contract that overwrote the proxy's implementation slot. Three experienced signers approved it. ([NCC Group analysis](https://www.nccgroup.com/research/in-depth-technical-analysis-of-the-bybit-hack/), [Blockworks](https://www.blockworks.com/news/security-firms-react-to-bybit-hack))

**Permit phishing, September 2024 ($32.4 million).** A single user lost 12,083 spWETH after signing an off-chain `permit` message on a phishing site operated with the Inferno Drainer kit. No transaction was sent by the victim at all. A typed, "readable" signature was enough to hand over the tokens. ([Cointelegraph](https://cointelegraph.com/news/crypto-phishing-attacks-q3-2024), [BeInCrypto](https://beincrypto.com/32-million-crypto-wallet-phishing-scam/))

In both cases the signer could not see what the signed data would actually *do* on-chain.

## Why EIP-712 is not enough

[EIP-712](https://eips.ethereum.org/EIPS/eip-712) was a big step forward: instead of an opaque hash, the wallet can show typed, named fields. But its limits sit exactly at the fault line between **intent** (the input) and **effect** (the state transition).

EIP-712 makes the static parameters of a call readable. It says nothing about what happens when those parameters hit the EVM. A field called `to` with an address and a field called `data` with some bytes are perfectly "readable", yet the Bybit signers still could not tell that `data` combined with `operation = 1` would rewrite the storage slot holding their wallet's implementation. NCC Group reached the same conclusion: EIP-712 cannot render nested operations and cannot decode what a complex contract call will do. ([NCC Group](https://www.nccgroup.com/research/in-depth-technical-analysis-of-the-bybit-hack/))

The permit case is even simpler. The message was well-typed and displayed, but the user had no way to see the consequence: an allowance that lets someone else move all their tokens.

What a user actually needs to know is the outcome: which balances change, which approvals are granted, which storage is written, which contracts are called. That is not something a signing format can provide. A simulation can.

## How TSA works

TSA turns a raw transaction into a verified, decoded and explained outcome in three steps.

1. **Simulate on verified state.** [`colibri_simulateTransaction`](https://corpus-core.gitbook.io/specification-colibri-stateless/specifications/ethereum/colibri-rpc-methods/colibri_simulatetransaction) takes the same arguments as `eth_call`. Colibri fetches a Merkle proof for every account and storage slot the transaction touches and executes it locally in its own EVM. Nothing is taken on faith from the RPC provider. The result records state changes, storage reads with their proven pre-state values, logs, internal calls, the verified `codeHash` of every account, keccak preimages of hashed storage keys, and the executed `JUMPDEST` program counters per code address.
2. **Map bytecode back to source.** For every touched contract we fetch the verified Solidity sources from [Sourcify](https://sourcify.dev/). We compile them and compare the resulting bytecode hash with the `codeHash` proven during simulation. Only if they match do we use the source. With the ABI, the storage layout and the keccak preimages, we can then decode every event, every call and every storage change: `balances[0xabc…]` instead of `slot 0x7f3e…`.
3. **Explain with a language model.** The prompt contains the decoded events, calls and state changes, and the Solidity code that actually ran. To keep the context small, we use the executed `JUMPDEST` positions and the compiler's source maps to include only the functions that were really executed, not the whole contract. Any LLM can answer this prompt and explain to the user, in plain language, what the transaction will do.

The important property: the model never has to guess what the transaction does. It explains a proven execution, backed by the exact code that produced it.

## A language model that runs in your browser

The explanation step works with any remote API. But our goal was that the whole pipeline runs locally, in the browser, with nothing to install.

So we fine-tuned our own small language model on top of [Qwen3.5-4B](https://apxml.com/models/qwen35-4b), the 4-billion-parameter member of Alibaba's open-weight Qwen3.5 family released in February 2026 under Apache 2.0. We trained it specifically on decoded simulation results and the matching Solidity code, then optimized and quantized it until it runs entirely client-side with [WebLLM](https://github.com/mlc-ai/web-llm), which executes models on the GPU through WebGPU.

The first time you use it, the browser downloads the model once (about 2.4 GB). After that it is cached, and every transaction can be simulated and explained without a single request leaving your machine for the explanation.

## Why local matters

**Trust.** Every simulation that runs on someone else's server, and every prompt sent to a third-party API, asks you to trust that the answer you get back is honest. That trust is fragile even when the provider is reputable. On 8 September 2025, a phished maintainer account was used to push malicious versions of `debug`, `chalk` and 16 other npm packages with over 2 billion weekly downloads combined. The injected code hooked `fetch`, `XMLHttpRequest` and `window.ethereum` in the browser, scanned responses for addresses and swapped them for lookalike attacker addresses before the user signed. ([Check Point](https://blog.checkpoint.com/crypto/the-great-npm-heist-september-2025/), [Cycode](https://cycode.com/blog/npm-debug-chalk-supply-chain-attack-the-complete-guide/)) An attack like this can silently rewrite a remote simulation result or an LLM's answer in transit. A simulation that is verified against proofs and an explanation generated on your own GPU leave no such channel.

**Privacy.** Simulating before signing only helps if the simulation does not leak the transaction. If you send the transaction or the prompt to a third party, you reveal your intent before it hits the chain. That opens the door to front-running and censorship. On top of that, the RPC provider learns your wallet address and, through the request, your IP address. Running simulation and explanation locally keeps the transaction yours until you decide to broadcast it.

## Try it yourself

We have put everything into a playground: [playground.colibri-proof.tech](https://playground.colibri-proof.tech/). Paste a transaction, let Colibri simulate it on proven state, and let the model in your browser tell you what it will do before you sign it.

Read before you sign. Dont't trust verify! Happy simulating!

## References

- [colibri\_simulateTransaction specification](https://corpus-core.gitbook.io/specification-colibri-stateless/specifications/ethereum/colibri-rpc-methods/colibri_simulatetransaction)
- [EIP-712: Typed structured data hashing and signing](https://eips.ethereum.org/EIPS/eip-712)
- [Sourcify](https://sourcify.dev/)
- [WebLLM](https://github.com/mlc-ai/web-llm)
- [Qwen3.5-4B model overview (ApX)](https://apxml.com/models/qwen35-4b)
- [NCC Group: In-Depth Technical Analysis of the Bybit Hack](https://www.nccgroup.com/research/in-depth-technical-analysis-of-the-bybit-hack/)
- [Blockworks: Security firms react to Bybit hack](https://www.blockworks.com/news/security-firms-react-to-bybit-hack)
- [Cointelegraph: Crypto phishing scams drained $46M in September 2024](https://cointelegraph.com/news/crypto-phishing-attacks-q3-2024)
- [BeInCrypto: $32 million spWETH permit phishing](https://beincrypto.com/32-million-crypto-wallet-phishing-scam/)
- [Check Point: The Great npm Heist, September 2025](https://blog.checkpoint.com/crypto/the-great-npm-heist-september-2025/)
- [Cycode: npm debug and chalk supply chain attack](https://cycode.com/blog/npm-debug-chalk-supply-chain-attack-the-complete-guide/)
- [Playground](https://playground.colibri-proof.tech/)
