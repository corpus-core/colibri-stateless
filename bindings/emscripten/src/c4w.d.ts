export interface C4W {
    _c4w_create_proof_ctx: (method: string, args: string, chainId: number) => number;
    _c4w_free_proof_ctx: (proofCtx: number) => void;
    _c4w_execute_proof_ctx: (proofCtx: number) => string;
    _c4w_get_pending_data_request: (proofCtx: number) => number;
}

export declare const C4WPromise: Promise<C4W>;

export default C4WPromise

