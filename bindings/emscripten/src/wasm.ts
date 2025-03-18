
export interface C4W {
    _c4w_create_proof_ctx: (method: number, args: number, chainId: bigint) => number;
    _c4w_free_proof_ctx: (proofCtx: number) => void;
    _c4w_execute_proof_ctx: (proofCtx: number) => number;
    _c4w_get_pending_data_request: (proofCtx: number) => number;
    _c4w_req_set_response: (reqPtr: number, data: number, len: number, node_index: number) => void;
    _c4w_req_set_error: (reqPtr: number, error: number, node_index: number) => void;

    _c4w_create_verify_ctx: (proof: number, proof_len: number, method: number, args: number, chain_id: bigint) => number;
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

export async function loadC4WModule(): Promise<C4W> {
    const module = (await import("./c4w.js")) as any;
    return module.default(); // Emscripten initializes the module
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
    return JSON.parse(str);
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