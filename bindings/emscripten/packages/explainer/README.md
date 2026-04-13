# @corpus-core/colibri-explainer

LLM-powered transaction simulation explainer for [Colibri Stateless](https://github.com/corpus-core/c4). Takes the JSON result of `colibri_simulateTransaction` and produces a human-readable explanation via configurable LLM providers (OpenAI, Anthropic, Ollama, or any OpenAI-compatible endpoint).

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
  provider: 'openai',        // 'openai' | 'anthropic' | 'ollama'
  apiKey: 'sk-...',          // API key (not needed for Ollama)

  // Optional
  model: 'gpt-4o-mini',     // Model name (provider-specific default if omitted)
  baseUrl: 'http://...',    // Custom endpoint (required for Ollama)
  language: 'de',           // Response language (default: English)
  maxTokens: 1024,          // Max response tokens

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

## Architecture

This package is intentionally separate from `@corpus-core/colibri-stateless` to preserve the core library's zero-dependency security model. The explainer uses only the native `fetch()` API -- no external SDKs are required.

```
@corpus-core/colibri-stateless  (zero dependencies, verifier)
         │
         │  SimulationResult JSON
         ▼
@corpus-core/colibri-explainer  (this package)
         │
         │  LLM API call via fetch()
         ▼
   OpenAI / Anthropic / Ollama
```

## License

MIT
