import { defineConfig } from 'vite';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));

// The compiled `@corpus-core/colibri-stateless` browser bundle is produced by
// the Emscripten/CMake build and is NOT published in this monorepo checkout.
// Point COLIBRI_DIST at the built `index.js` (with embedded WASM, SINGLE_FILE=1),
// or rely on the default location produced by `cmake --preset wasm`.
const colibriDist =
    process.env.COLIBRI_DIST ||
    resolve(here, '../../../../build/wasm/emscripten/index.js');

export default defineConfig({
    resolve: {
        alias: {
            '@corpus-core/colibri-stateless': colibriDist,
        },
    },
    // WebLLM ships its own worker/WASM assets; let Vite serve them as-is rather
    // than trying to pre-bundle the large dependency.
    optimizeDeps: {
        exclude: ['@mlc-ai/web-llm'],
    },
    server: {
        port: 5173,
    },
});
