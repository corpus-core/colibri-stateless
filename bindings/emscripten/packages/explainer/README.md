# @corpus-core/colibri-explainer

LLM-powered transaction simulation explainer for [Colibri Stateless](https://github.com/corpus-core/colibri-stateless). Takes the JSON result of `colibri_simulateTransaction` and produces a human-readable explanation via configurable LLM providers: OpenAI, Anthropic, Ollama, any OpenAI-compatible endpoint, or a fully local in-browser model running on WebGPU.

Before prompting the model, the package fetches the verified contract sources from Sourcify, compiles and verifies them, extracts the storage layout, maps the raw state changes onto named contract variables, and decodes calls/events/revert reasons -- so the LLM receives rich, structured context instead of opaque hex.

## Installation

```bash
npm install @corpus-core/colibri-explainer
```

## Usage

```typescript
import { C4Client } from '@corpus-core/colibri-stateless';
import { explainSimulation } from '@corpus-core/colibri-explainer';

const client = new C4Client({ /* chain config */ });
const tx = {
  to: '0xC02aaA39b223FE8D0A0e5C4F27eAD9083C756Cc2',
  data: '0xd0e30db0',
  value: '0x16345785d8a0000',
  from: '0x3610bad33Aac567d2c5Fb03e47EEc5c2172fD42a'
};
const result = await client.rpc('colibri_simulateTransaction', [tx, 'latest']);

const explanation = await explainSimulation(result, tx, {
  provider: 'openai',
  apiKey: process.env.OPENAI_API_KEY,
  model: 'gpt-4o-mini'
});

console.log(explanation);
// "This transaction deposits 0.1 ETH into the WETH contract and
//  receives 0.1 WETH (Wrapped Ether) in return. Gas cost: ~45,038 gas."
```

## Configuration

```typescript
const explanation = await explainSimulation(result, tx, {
  // Required: LLM provider
  provider: 'openai',        // 'openai' | 'anthropic' | 'ollama' | 'webllm'
  apiKey: 'sk-...',          // API key (not needed for Ollama/WebLLM)

  // Optional
  model: 'gpt-4o-mini',     // Model name (provider-specific default if omitted)
  baseUrl: 'http://...',    // Custom endpoint (required for Ollama)
  language: 'de',           // Response language (default: English)
  maxTokens: 1024,          // Max response tokens
  maxSourceChars: 10000,    // Source-code budget embedded in the prompt

  // App-specific context appended to the system prompt
  systemPromptInclude: 'This is a DeFi wallet. Focus on user-facing financial impact.'
});
```

### Ollama (local)

```typescript
const explanation = await explainSimulation(result, tx, {
  provider: 'ollama',
  baseUrl: 'http://localhost:11434',
  model: 'llama3.1'
});
```

### Local / in-browser (WebGPU via WebLLM)

Runs a quantized model fully on-device with WebGPU, so the **prompt and
explanation never leave the browser** -- no LLM service is contacted. Requires a
WebGPU-capable browser (recent Chrome/Edge).

`@mlc-ai/web-llm` is declared as an optional dependency and is installed by
default; it is loaded lazily (dynamic `import`) only when the `webllm` provider
is actually used. Skip it with `npm install --omit=optional` if you never use
the local provider.

> Note on privacy: the LLM inference is local, but setting `chainId` enables
> Sourcify enrichment, which fetches public contract metadata (by address /
> code hash) over the network. Omit `chainId` for a fully offline run.

```typescript
const explanation = await explainSimulation(result, tx, {
  provider: 'webllm',
  // Code-tuned 7B model (~5-6 GB VRAM). Use 'Llama-3.2-3B-Instruct-q4f16_1-MLC'
  // (~2.3 GB) for lower-end devices. Defaults to Qwen2.5-Coder-7B if omitted.
  model: 'Qwen2.5-Coder-7B-Instruct-q4f16_1-MLC',
  chainId: 1,
  // Many prebuilt models default to a 4096-token context. Either raise it...
  contextWindowSize: 8192,
  // ...and/or shrink the embedded source-code budget (default 10000 chars).
  maxSourceChars: 4000,
  // Progress for the one-time model download (cached afterwards).
  onModelProgress: ({ progress, text }) => console.log(`${Math.round(progress * 100)}% ${text}`),
});
```

The model is downloaded once and cached by the browser. To reuse an
already-initialized engine across calls, pass it via `webllmEngine`.

## Architecture

This package is intentionally separate from `@corpus-core/colibri-stateless` so the
core verifier keeps its zero-dependency security model. `@corpus-core/colibri-stateless`
is an optional peer dependency -- the explainer only consumes the `SimulationResult`
JSON it produces and never imports the verifier itself.

The explainer brings its own runtime dependencies for the enrichment step:

- [`ethers`](https://www.npmjs.com/package/ethers) and
  [`@solidity-parser/parser`](https://www.npmjs.com/package/@solidity-parser/parser)
  -- ABI/event decoding and storage-slot resolution.
- [`solc`](https://www.npmjs.com/package/solc) -- compiles the fetched sources to
  verify the on-chain bytecode and to extract the storage layout.
- [`@mlc-ai/web-llm`](https://www.npmjs.com/package/@mlc-ai/web-llm) (optional) --
  loaded lazily only for the `webllm` provider.

Cloud providers (OpenAI/Anthropic/Ollama) are reached via the native `fetch()` API;
no provider SDKs are bundled.

```
@corpus-core/colibri-stateless  (zero dependencies, verifier)
         │
         │  SimulationResult JSON
         ▼
@corpus-core/colibri-explainer  (this package)
         │  fetch Sourcify sources → solc compile + verify → storage layout
         │  decode calls/events/state changes → build prompt
         ▼
   OpenAI / Anthropic / Ollama (fetch)   ·   WebLLM (local, WebGPU)
```

## Publishing

This package is published to npm on every tagged release (`vX.Y.Z`) via the
`colibri-explainer` job in
[`.github/workflows/bindings-emscripten.yml`](../../../../.github/workflows/bindings-emscripten.yml),
mirroring the other Colibri JS packages. The release tag sets the version.

## License

MIT
