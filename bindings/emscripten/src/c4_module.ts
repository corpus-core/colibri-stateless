export interface C4W {
    _c4w_create_proof_ctx: (method: number, args: number, chainId: bigint) => number;
    _c4w_free_proof_ctx: (proofCtx: number) => void;
    _c4w_execute_proof_ctx: (proofCtx: number) => number;
    _c4w_get_pending_data_request: (proofCtx: number) => number;
    _c4w_req_set_response: (reqPtr: number, data: number, len: number) => void;
    _c4w_req_set_error: (reqPtr: number, error: string) => void;

    HEAPU8: Uint8Array;
    stringToUTF8: (str: string, ptr: number, length: number) => number;
    UTF8ToString: (ptr: number) => string;
    _malloc: (size: number) => number;
    _free: (ptr: number) => void;
}

export type C4WModule = {
    then: (cb: (mod: C4W) => void) => void;
};

export async function loadC4WModule(): Promise<C4W> {
    const module = (await import("./c4w.js")) as any;
    return module.default(); // Emscripten initializes the module
}