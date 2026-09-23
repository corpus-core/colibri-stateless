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

## Docker

A self-contained image (static site served by nginx) is built in CI and
published to GHCR. It bundles the colibri-stateless WASM and the playground UI;
the LLM model is still downloaded client-side by the browser on first use.

```bash
# dev branch -> :dev tag, releases -> :<version> + :latest
docker run --rm -p 8080:80 ghcr.io/corpus-core/colibri-explainer-playground:dev
# open http://localhost:8080
```

Build it locally:

```bash
# from the repo root
docker build -f bindings/docker/Dockerfile.playground -t colibri-explainer-playground .
docker run --rm -p 8080:80 colibri-explainer-playground
```

## Model sizing

The explainer pre-processes the prompt heavily (decoded calls, resolved storage
variables, optional source snippets), so a mid-size model is enough:

| Model | VRAM (~4-bit) | Notes |
| --- | --- | --- |
| `colibri-tsa-4b` | ~4.4 GB (32k context) | Fine-tuned on explainer prompts (default) |
| `Llama-3.2-3B` / `Qwen2.5-Coder-3B` | ~2.3-3 GB | Generic, lower usable bound |
| `Qwen2.5-Coder-7B-Instruct` | ~5-6 GB | Generic, code-tuned |

The fine-tuned entries come from `TSA_EXPLAINER_MODELS` in the explainer
package and appear first in the dropdown; selecting one prefills **Context
window** with the value the model was packaged for. Their weights are loaded,
in this order, from `?modelUrl=<base>` (explicit override, e.g. a locally
converted model served by `tsa_train.py serve`), from the same origin at
`/models/<model_id>/<weights_version>/` when a model server is deployed behind
the playground's reverse proxy (probed once on page load), or from the record's
Hugging Face URL. Prebuilt models default to
4096 tokens: if that is exceeded, lower **Max source chars** and/or raise the
**Context window** field.

## License

MIT
