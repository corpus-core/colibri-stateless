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

// This file is used only for the CommonJS build output. It must not use
// `import.meta` because that is a parse error in CommonJS environments (e.g. Jest).

import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import {
    as_bytes,
    as_char_ptr,
    as_json,
    copy_to_c,
    createC4wApi,
    get_default_storage,
    type C4W,
    type C4WModule,
    type Storage,
} from './wasm_shared.js';

export type { C4W, C4WModule, Storage };
export { as_bytes, as_char_ptr, as_json, copy_to_c, get_default_storage };

async function dynamicImport(specifier: string): Promise<any> {
    // Prevent TypeScript from transforming `import()` into `require()` in CJS output.
    // Node supports dynamic import in CommonJS.
    const importer = new Function('p', 'return import(p)') as (p: string) => Promise<any>;
    return importer(specifier);
}

const api = createC4wApi({
    importC4wModule: async () => {
        const pkgRoot = join(__dirname, '..');
        const glueUrl = pathToFileURL(join(pkgRoot, 'c4w.js')).href;
        return await dynamicImport(glueUrl);
    },
    resolveWasmLocation: (override: string | null) => {
        // In CJS/Node we provide wasmBinary, so we don't need locateFile here.
        // Keep override support for browser-like loaders (if someone uses it).
        if (override) return override;
        return null;
    },
    getWasmBinary: async (override: string | null) => {
        const fs = await dynamicImport('node:fs');
        const wasmPath = override ?? join(__dirname, '..', 'c4w.wasm');
        return fs.readFileSync(wasmPath);
    },
});

export const set_wasm_url = api.set_wasm_url;
export const loadC4WModule = api.loadC4WModule;
export const getC4w = api.getC4w;
export const get_prover_config_hex = api.get_prover_config_hex;
export const set_trusted_checkpoint = api.set_trusted_checkpoint;

