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

export interface C4W {
    _c4w_create_proof_ctx: (method: number, args: number, chainId: bigint, flags: number) => number;
    _c4w_free_proof_ctx: (proofCtx: number) => void;
    _c4w_execute_proof_ctx: (proofCtx: number) => number;
    _c4w_get_pending_data_request: (proofCtx: number) => number;
    _c4w_req_set_response: (reqPtr: number, data: number, len: number, node_index: number) => void;
    _c4w_req_set_error: (reqPtr: number, error: number, node_index: number) => void;
    _c4w_get_method_type: (chain_id: bigint, method: number) => number;
    _c4w_create_verify_ctx: (proof: number, proof_len: number, method: number, args: number, chain_id: bigint, trusted_checkpoint: number) => number;
    _c4w_free_verify_ctx: (verifyCtx: number) => void;
    _c4w_verify_proof: (verifyCtx: number) => number;
    _c4w_req_free: (reqPtr: number) => void;
    _init_storage: () => void;
    HEAPU8: Uint8Array;
    stringToUTF8: (str: string, ptr: number, length: number) => number;
    UTF8ToString: (ptr: number) => string;
    _malloc: (size: number) => number;
    _free: (ptr: number) => void;
    storage: Storage;
}

export interface Storage {
    get: (key: string) => Uint8Array | null;
    set: (key: string, value: Uint8Array) => void;
    del: (key: string) => void;
}

export type C4WModule = {
    then: (cb: (mod: C4W) => void) => void;
};

let wasmUrlOverride: string | null = null;
/**
 * Optionally override the URL/path used to load the WASM binary when not embedded.
 * @param url Absolute/relative URL in browsers, or filesystem path in Node.
 */
export function set_wasm_url(url: string) {
    wasmUrlOverride = url;
}

export async function loadC4WModule(): Promise<C4W> {
    const module = (await import("./c4w.js")) as any;

    const args: any = {};
    if (wasmUrlOverride) {
        args.locateFile = (path: string) => path.endsWith('.wasm') ? (wasmUrlOverride as string) : path;
    } else if (isBrowserEnvironment()) {
        // Default browser-friendly resolution so bundlers copy the asset without extra config
        args.locateFile = (path: string) => path.endsWith('.wasm') ? new URL('./c4w.wasm', import.meta.url).toString() : path;
    }
    return module.default(args); // Emscripten initializes the module
}

let module: C4W | null = null;
let modulePromise: Promise<C4W> | null = null;
export async function getC4w(): Promise<C4W> {
    if (module) return module;
    if (!modulePromise) {
        modulePromise = loadC4WModule().then(async (loadedModule) => {
            module = loadedModule;
            module.storage = await get_default_storage();
            module._init_storage();
            modulePromise = null; // Reset the promise after loading
            return module;
        });
    }
    return modulePromise;
}
function isNodeEnvironment() {
    return (typeof process !== 'undefined' && process.versions != null && process.versions.node != null);
}

function isBrowserEnvironment() {
    return (typeof window !== 'undefined' && typeof document !== 'undefined');
}

export async function get_default_storage(): Promise<Storage> {
    if (isBrowserEnvironment())
        // web interface
        return {
            get: (key: string) => {
                const value = window.localStorage.getItem(key);
                if (value) {
                    const length = value.length / 2;
                    const uint8Array = new Uint8Array(length);
                    for (let i = 0; i < length; i++) {
                        uint8Array[i] = parseInt(value.substr(i * 2, 2), 16);
                    }
                    return uint8Array;
                }
                return null;
            },
            set: (key: string, value: Uint8Array) => {
                window.localStorage.setItem(key, Array.from(value).map(_ => _.toString(16).padStart(2, '0')).join(''));
            },
            del: (key: string) => {
                window.localStorage.removeItem(key);
            },
        };

    else if (isNodeEnvironment()) {
        const fs = await import('fs');
        // node interface
        return {
            get: (key: string) => {
                try {
                    return fs.readFileSync(key);
                } catch (e) {
                    return null;
                }
            },
            set: (key: string, value: Uint8Array) => {
                fs.writeFileSync(key, value);
            },
            del: (key: string) => {
                fs.unlinkSync(key);
            },
        };
    }
    throw new Error('Unsupported environment');
}
export function as_char_ptr(str: string, c4w: C4W, free_ptrs?: number[]) {
    const ptr = c4w._malloc(str.length + 1);
    c4w.stringToUTF8(str, ptr, str.length + 1);
    if (free_ptrs) free_ptrs.push(ptr);
    return ptr;
}

export function as_json(ptr: number, c4w: C4W, free_ptrs?: number[] | boolean): any {
    const str = c4w.UTF8ToString(ptr);
    if (free_ptrs) {
        if (Array.isArray(free_ptrs)) free_ptrs.push(ptr);
        else c4w._free(ptr);
    }
    try {
        return JSON.parse(str);
    } catch (e) {
        console.error(e);
        console.error(str);
        return null;
    }
}

export function as_bytes(ptr: number, len: number, c4w: C4W, free_ptrs?: number[] | boolean): Uint8Array {
    const bytes = new Uint8Array(len);
    bytes.set(c4w.HEAPU8.subarray(ptr, ptr + len));
    if (free_ptrs) {
        if (Array.isArray(free_ptrs)) free_ptrs.push(ptr);
        else c4w._free(ptr);
    }
    return bytes;
}

export function copy_to_c(data: Uint8Array, c4w: C4W, free_ptrs?: number[]): number {
    const ptr = c4w._malloc(data.length + 1);
    c4w.HEAPU8.set(data, ptr);
    c4w.HEAPU8[ptr + data.length] = 0;
    if (free_ptrs) free_ptrs.push(ptr);
    return ptr;
}

/**
 * Returns the prover config state for a given chain as hex string with 0x prefix.
 * @param chainId Chain identifier
 * @return Hex string (e.g. "0x...") or "0x" if no state present
 */
export async function get_prover_config_hex(chainId: number): Promise<string> {
    const c4w = await getC4w();
    if (!c4w.storage) return '0x';
    const state = c4w.storage.get('states_' + chainId);
    return '0x' + (state ? Array.from(state).map(_ => _.toString(16).padStart(2, '0')).join('') : '');
}

/**
 * Sets the trusted checkpoint inside the C context to initialize state.
 * @param chainId Chain identifier
 * @param checkpoint Trusted checkpoint root hex string
 */
export async function set_trusted_checkpoint(chainId: number, checkpoint: string): Promise<void> {
    const c4w = await getC4w();
    const free_buffers: number[] = [];
    c4w._c4w_create_verify_ctx(0, 0, 0, 0, BigInt(chainId), as_char_ptr(checkpoint, c4w, free_buffers));
    free_buffers.forEach(ptr => c4w._free(ptr));
}