/**
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

import {
    as_bytes,
    as_char_ptr,
    as_json,
    copy_to_c,
    createC4wApi,
    get_default_storage,
    isBrowserEnvironment,
    isNodeEnvironment,
    type C4W,
    type C4WModule,
    type Storage,
} from './wasm_shared.js';

export type { C4W, C4WModule, Storage };
export { as_bytes, as_char_ptr, as_json, copy_to_c, get_default_storage };

const api = createC4wApi({
    importC4wModule: async () => (await import("./c4w.js")) as any,
    resolveWasmLocation: (override: string | null) => {
        if (override) return override;
        if (isBrowserEnvironment()) {
            // Default browser-friendly resolution so bundlers copy the asset without extra config
            return new URL('./c4w.wasm', import.meta.url).toString();
        }
        return null;
    },
    getWasmBinary: async (override: string | null) => {
        // When the Emscripten glue is built for web/worker only, Node cannot use its
        // internal fs-based loader. In Node we provide the bytes via wasmBinary.
        if (!isNodeEnvironment()) return null;

        const fs = await (new Function('return import("node:fs")') as () => Promise<any>)();

        let wasmPath: string;
        if (override) {
            if (override.startsWith('file:')) wasmPath = decodeURIComponent(new URL(override).pathname);
            else wasmPath = override;
        } else {
            const wasmUrl = new URL('./c4w.wasm', import.meta.url);
            wasmPath = wasmUrl.protocol === 'file:' ? decodeURIComponent(wasmUrl.pathname) : wasmUrl.toString();
        }

        // Emscripten accepts Uint8Array/ArrayBuffer in Module.wasmBinary.
        return fs.readFileSync(wasmPath);
    },
});

export const set_wasm_url = api.set_wasm_url;
export const loadC4WModule = api.loadC4WModule;
export const getC4w = api.getC4w;
export const get_prover_config_hex = api.get_prover_config_hex;
export const set_trusted_checkpoint = api.set_trusted_checkpoint;