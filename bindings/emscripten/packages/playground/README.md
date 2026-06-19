# Colibri Transaction Explainer Playground

A small browser playground for [`@corpus-core/colibri-explainer`](../explainer). Enter a
raw transaction or a transaction object, run `colibri_simulateTransaction`
(verified, via `@corpus-core/colibri-stateless`), and get a human-readable
explanation from either a **local WebGPU model** (WebLLM) or a cloud provider
(OpenAI / Anthropic / Ollama).

The raw `colibri_simulateTransaction` JSON and the decoded/enriched result are
available in a collapsible debug panel.

## Prerequisites

The `@corpus-core/colibri-stateless` browser bundle is produced by the
Emscripten/CMake build and is **not** published inside this monorepo checkout.
Build it first:

```bash
# from the repo root
cmake --preset wasm
cmake --build build/wasm
```

This produces `build/wasm/emscripten/index.js` (with the WASM embedded,
`SINGLE_FILE=1`), which the playground resolves by default. To point at a
different build, set `COLIBRI_DIST`:

```bash
COLIBRI_DIST=/abs/path/to/emscripten/index.js npm run dev
```

The explainer package must be built as well (the playground depends on its
`dist/`):

```bash
cd ../explainer && npm install && npm run build
```

## Run

```bash
npm install
npm run dev
```

Open the printed URL (default <http://localhost:5173>) in a **WebGPU-capable
browser** (recent Chrome/Edge) for the local model. The first run downloads the
selected model (cached afterwards in the browser); a progress bar shows the
status.

## Model sizing

The explainer pre-processes the prompt heavily (decoded calls, resolved storage
variables, optional source snippets), so a mid-size model is enough:

| Model | VRAM (~4-bit) | Notes |
| --- | --- | --- |
| `Llama-3.2-3B` / `Qwen2.5-Coder-3B` | ~2.3-3 GB | Lower usable bound |
| `Qwen2.5-Coder-7B-Instruct` | ~5-6 GB | Best quality, code-tuned (default) |

If a small context window (many prebuilt models default to 4096 tokens) is
exceeded, lower **Max source chars** and/or raise the **Context window** field.

## License

MIT
