import { defineConfig } from 'vite';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(here, '../../../../');

// The compiled `@corpus-core/colibri-stateless` browser bundle is produced by
// the Emscripten/CMake build and is NOT published in this monorepo checkout.
// It is resolved via the alias below (not from node_modules), so the playground
// always loads exactly the bundle that was just built on disk.
//
//   - `cmake --preset wasm`        -> build/wasm/emscripten/index.js (default,
//                                     WASM_EMBED=ON, single-file, no extra assets)
//   - `cmake --preset wasm-debug`  -> build/wasm-debug/emscripten/index.js
//                                     (WASM_EMBED=OFF -> separate c4w.wasm +
//                                      c4w.wasm.debug.wasm DWARF for the debugger)
//
// Override the location with the COLIBRI_DIST env var, e.g.:
//   COLIBRI_DIST="$PWD/build/wasm-debug/emscripten/index.js" npm run dev
const colibriDist =
    process.env.COLIBRI_DIST ||
    resolve(repoRoot, 'build/wasm/emscripten/index.js');
const colibriDir = dirname(colibriDist);

export default defineConfig({
    resolve: {
        alias: {
            '@corpus-core/colibri-stateless': colibriDist,
        },
    },
    optimizeDeps: {
        // WebLLM ships its own worker/WASM assets; serve them as-is.
        // The colibri bundle must NOT be pre-bundled either: for non-embedded
        // builds it locates its sibling `c4w.wasm` via `new URL('./c4w.wasm',
        // import.meta.url)`, which only resolves correctly when the module is
        // served from its real on-disk location instead of `.vite/deps`.
        exclude: ['@mlc-ai/web-llm', '@corpus-core/colibri-stateless'],
    },
    server: {
        port: 5173,
        fs: {
            // The colibri bundle lives outside the playground package, and the
            // debug build additionally serves its sibling c4w.wasm / DWARF file.
            // Allow the repo root (covers build output, the explainer package and
            // hoisted node_modules) so Vite can read all of them.
            allow: [repoRoot, colibriDir],
        },
    },
});
