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
    _c4w_get_method_type: (chain_id: bigint, method: number, params: number, flags: number) => number;
    _c4w_create_verify_ctx: (proof: number, proof_len: number, method: number, args: number, chain_id: bigint, trusted_checkpoint: number, witness_keys: number, flags: number, min_latest_block_ts: bigint) => number;
    _c4w_free_verify_ctx: (verifyCtx: number) => void;
    _c4w_verify_proof: (verifyCtx: number) => number;
    _c4w_req_free: (reqPtr: number) => void;
    _c4w_get_current_version_number: () => number;
    _c4w_create_rpc_ctx: (method: number, params: number, chainId: bigint, prover_flags: number, verify_flags: number, prover_mode: number) => number;
    _c4w_execute_rpc_ctx: (ctx: number) => number;
    _c4w_free_rpc_ctx: (ctx: number) => void;
    _c4w_set_checkpoint: (chainId: bigint, checkpoint: number) => void;
    _c4w_reset_caches: () => void;
    _c4w_rpc_ctx_set_witness_keys: (ctx: number, keys: number) => void;
    _c4w_rpc_ctx_set_proxy_urls: (ctx: number, rpc_urls: number, beacon_urls: number) => void;
    _c4w_rpc_ctx_set_min_latest_block_ts: (ctx: number, ts: bigint) => void;
    _c4w_decode_proof: (data: number, len: number) => number;
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

export function isNodeEnvironment() {
    return (typeof process !== 'undefined' && process.versions != null && process.versions.node != null);
}

export function isBrowserEnvironment() {
    return (typeof window !== 'undefined' && typeof document !== 'undefined');
}

function hasLocalStorage(): boolean {
    try {
        return typeof globalThis !== 'undefined' && !!(globalThis as any).localStorage;
    } catch {
        return false;
    }
}

function hasCacheApi(): boolean {
    try {
        return typeof globalThis !== 'undefined' && 'caches' in globalThis;
    } catch {
        return false;
    }
}

// Keys from C are always alphanumeric + underscore (e.g. "states_1", "sync_1_234"),
// so no URL encoding is needed.
const CACHE_URL_PREFIX = 'http://colibri.storage/';

async function loadCacheApiStorage(): Promise<Storage | null> {
    try {
        const cache = await globalThis.caches.open('colibri-storage');
        const mem = new Map<string, Uint8Array>();
        const requests = await cache.keys();
        await Promise.all(requests.map(async (req) => {
            const resp = await cache.match(req);
            if (resp) {
                const key = req.url.slice(CACHE_URL_PREFIX.length);
                mem.set(key, new Uint8Array(await resp.arrayBuffer()));
            }
        }));
        return {
            get: (key: string) => mem.get(key) ?? null,
            set: (key: string, value: Uint8Array) => {
                const copy = new Uint8Array(value);
                mem.set(key, copy);
                cache.put(CACHE_URL_PREFIX + key, new Response(copy, {
                    headers: {'Content-Type': 'application/octet-stream', 'Content-Length': '' + copy.byteLength},
                })).catch(
                    e => console.warn('colibri: cache write failed for', key, e));
            },
            del: (key: string) => {
                mem.delete(key);
                cache.delete(CACHE_URL_PREFIX + key).catch(
                    e => console.warn('colibri: cache delete failed for', key, e));
            },
        };
    } catch {
        return null;
    }
}

async function importNodeFs(): Promise<any> {
    // Avoid bundlers (Webpack/Metro) trying to resolve Node builtins like 'fs'
    // when targeting browsers / react-native-web.
    // This only executes in Node, guarded by isNodeEnvironment().
    const importer = new Function('return import("node:fs")') as () => Promise<any>;
    return importer();
}

export async function get_default_storage(): Promise<Storage> {
    if (hasCacheApi()) {
        const storage = await loadCacheApiStorage();
        if (storage) return storage;
    }

    if (hasLocalStorage())
        return {
            get: (key: string) => {
                const ls = (globalThis as any).localStorage as {
                    getItem: (k: string) => string | null;
                    setItem: (k: string, v: string) => void;
                    removeItem: (k: string) => void;
                };
                const value = ls.getItem(key);
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
                const ls = (globalThis as any).localStorage as {
                    getItem: (k: string) => string | null;
                    setItem: (k: string, v: string) => void;
                    removeItem: (k: string) => void;
                };
                ls.setItem(key, Array.from(value).map(_ => _.toString(16).padStart(2, '0')).join(''));
            },
            del: (key: string) => {
                const ls = (globalThis as any).localStorage as {
                    getItem: (k: string) => string | null;
                    setItem: (k: string, v: string) => void;
                    removeItem: (k: string) => void;
                };
                ls.removeItem(key);
            },
        };

    if (isNodeEnvironment()) {
        const fs = await importNodeFs();
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

    // Fallback for web-like runtimes without localStorage (e.g. Chrome extension MV3 service workers).
    // This keeps the API usable but does not persist across restarts.
    const mem = new Map<string, Uint8Array>();
    return {
        get: (key: string) => mem.get(key) ?? null,
        set: (key: string, value: Uint8Array) => mem.set(key, new Uint8Array(value)),
        del: (key: string) => {
            mem.delete(key);
        },
    };
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

export function createC4wApi(options: {
    importC4wModule: () => Promise<any>,
    resolveWasmLocation: (override: string | null) => string | null,
    getWasmBinary?: (override: string | null) => Promise<Uint8Array | ArrayBuffer | null>,
}) {
    let wasmUrlOverride: string | null = null;
    let moduleInstance: C4W | null = null;
    let modulePromise: Promise<C4W> | null = null;

    function set_wasm_url(url: string) {
        wasmUrlOverride = url;
    }

    async function loadC4WModule(): Promise<C4W> {
        const module = await options.importC4wModule();
        const args: any = {};

        if (options.getWasmBinary) {
            const wasmBinary = await options.getWasmBinary(wasmUrlOverride);
            if (wasmBinary) {
                args.wasmBinary = wasmBinary;
            }
        }

        const wasmLocation = options.resolveWasmLocation(wasmUrlOverride);
        if (wasmLocation) {
            args.locateFile = (path: string) => path.endsWith('.wasm') ? wasmLocation : path;
        }

        return module.default(args);
    }

    async function getC4w(): Promise<C4W> {
        if (moduleInstance) return moduleInstance;
        if (!modulePromise) {
            modulePromise = loadC4WModule().then(async (loadedModule) => {
                moduleInstance = loadedModule;
                moduleInstance.storage = await get_default_storage();
                moduleInstance._init_storage();
                modulePromise = null;
                return moduleInstance;
            });
        }
        return modulePromise;
    }

    /**
     * Returns the prover config state for a given chain as hex string with 0x prefix.
     * @param chainId Chain identifier
     * @return Hex string (e.g. "0x...") or "0x" if no state present
     */
    async function get_prover_config_hex(chainId: number): Promise<string> {
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
    async function set_trusted_checkpoint(chainId: number, checkpoint: string): Promise<void> {
        const c4w = await getC4w();
        const free_buffers: number[] = [];
        c4w._c4w_set_checkpoint(BigInt(chainId), as_char_ptr(checkpoint, c4w, free_buffers));
        free_buffers.forEach(ptr => c4w._free(ptr));
    }

    /**
     * Clears in-process prover/verifier caches (header tags, EL headers, PAP
     * tx cache, prover cache). Persistent storage is left to the host.
     */
    async function reset_caches(): Promise<void> {
        const c4w = await getC4w();
        c4w._c4w_reset_caches();
    }

    async function decode_proof(proof: Uint8Array): Promise<any> {
        const c4w = await getC4w();
        const ptr = copy_to_c(proof, c4w);
        const resultPtr = c4w._c4w_decode_proof(ptr, proof.length);
        c4w._free(ptr);
        if (!resultPtr) throw new Error('Unknown proof format');
        return as_json(resultPtr, c4w, true);
    }

    return {
        set_wasm_url,
        loadC4WModule,
        getC4w,
        get_prover_config_hex,
        set_trusted_checkpoint,
        reset_caches,
        decode_proof,
    };
}

