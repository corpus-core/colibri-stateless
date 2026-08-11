/**
 * Copyright (c) 2026 corpus.core
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

import { as_bytes, as_char_ptr, as_json, copy_to_c, getC4w, type C4W } from './wasm.js';
import type { C4Runtime, CtxHandle, RuntimeStatus, Storage } from './runtime.js';
import type { DataRequest } from './types.js';

// The WASM runtime wraps the pointer-level Emscripten API (`C4W`) behind the
// value-level `C4Runtime` interface. The module is loaded once via `getC4w()`.

function createRuntime(c4w: C4W): C4Runtime {
    return {
        kind: 'wasm',

        getMethodType(chainId, method, paramsJson, flags) {
            const free_buffers: number[] = [];
            const type = c4w._c4w_get_method_type(
                chainId,
                as_char_ptr(method, c4w, free_buffers),
                paramsJson ? as_char_ptr(paramsJson, c4w, free_buffers) : 0,
                flags
            );
            free_buffers.forEach(ptr => c4w._free(ptr));
            return type;
        },

        createProverCtx(method, argsJson, chainId, flags) {
            const free_buffers: number[] = [];
            const ctx = c4w._c4w_create_proof_ctx(
                as_char_ptr(method, c4w, free_buffers),
                as_char_ptr(argsJson, c4w, free_buffers),
                chainId,
                flags
            );
            free_buffers.forEach(ptr => c4w._free(ptr));
            return ctx;
        },

        executeProverCtx(ctx: CtxHandle): RuntimeStatus {
            const state = as_json(c4w._c4w_execute_proof_ctx(ctx as number), c4w, true);
            // On success the JSON carries the proof as heap pointer + length
            // (owned by the prover ctx); materialize it as bytes.
            if (state.status === 'success')
                state.result = as_bytes(state.result, state.result_len, c4w);
            return state;
        },

        freeProverCtx(ctx: CtxHandle) {
            c4w._c4w_free_proof_ctx(ctx as number);
        },

        createVerifyCtx(proof, method, argsJson, chainId, trustedCheckpoint, witnessKeys, flags, minLatestBlockTs) {
            const free_buffers: number[] = [];
            const ctx = c4w._c4w_create_verify_ctx(
                copy_to_c(proof, c4w, free_buffers),
                proof.length,
                as_char_ptr(method, c4w, free_buffers),
                as_char_ptr(argsJson, c4w, free_buffers),
                chainId,
                trustedCheckpoint ? as_char_ptr(trustedCheckpoint, c4w, free_buffers) : 0,
                witnessKeys ? as_char_ptr(witnessKeys, c4w, free_buffers) : 0,
                flags,
                minLatestBlockTs
            );
            free_buffers.forEach(ptr => c4w._free(ptr));
            return ctx;
        },

        verifyProof(ctx: CtxHandle): RuntimeStatus {
            return as_json(c4w._c4w_verify_proof(ctx as number), c4w, true);
        },

        freeVerifyCtx(ctx: CtxHandle) {
            c4w._c4w_free_verify_ctx(ctx as number);
        },

        createRpcCtx(method, paramsJson, chainId, proverFlags, verifyFlags, proverMode) {
            const free_buffers: number[] = [];
            const ctx = c4w._c4w_create_rpc_ctx(
                as_char_ptr(method, c4w, free_buffers),
                as_char_ptr(paramsJson, c4w, free_buffers),
                chainId,
                proverFlags,
                verifyFlags,
                proverMode
            );
            free_buffers.forEach(ptr => c4w._free(ptr));
            return ctx;
        },

        executeRpcCtx(ctx: CtxHandle): RuntimeStatus {
            return as_json(c4w._c4w_execute_rpc_ctx(ctx as number), c4w, true);
        },

        freeRpcCtx(ctx: CtxHandle) {
            c4w._c4w_free_rpc_ctx(ctx as number);
        },

        rpcCtxSetProxyUrls(ctx: CtxHandle, rpcUrls: string, beaconUrls: string) {
            const free_buffers: number[] = [];
            c4w._c4w_rpc_ctx_set_proxy_urls(
                ctx as number,
                as_char_ptr(rpcUrls, c4w, free_buffers),
                as_char_ptr(beaconUrls, c4w, free_buffers)
            );
            free_buffers.forEach(ptr => c4w._free(ptr));
        },

        rpcCtxSetWitnessKeys(ctx: CtxHandle, keys: string) {
            const free_buffers: number[] = [];
            c4w._c4w_rpc_ctx_set_witness_keys(ctx as number, as_char_ptr(keys, c4w, free_buffers));
            free_buffers.forEach(ptr => c4w._free(ptr));
        },

        rpcCtxSetMinLatestBlockTs(ctx: CtxHandle, ts: bigint) {
            c4w._c4w_rpc_ctx_set_min_latest_block_ts(ctx as number, ts);
        },

        setCheckpoint(chainId: bigint, checkpoint: string) {
            const free_buffers: number[] = [];
            c4w._c4w_set_checkpoint(chainId, as_char_ptr(checkpoint, c4w, free_buffers));
            free_buffers.forEach(ptr => c4w._free(ptr));
        },

        reqSetResponse(req: DataRequest, data: Uint8Array, nodeIndex: number) {
            // ownership of the copied bytes is transferred to the C context
            c4w._c4w_req_set_response(req.req_ptr as number, copy_to_c(data, c4w), data.length, nodeIndex);
        },

        reqSetError(req: DataRequest, error: string, nodeIndex: number) {
            const free_buffers: number[] = [];
            c4w._c4w_req_set_error(req.req_ptr as number, as_char_ptr(error, c4w, free_buffers), nodeIndex);
            free_buffers.forEach(ptr => c4w._free(ptr));
        },

        decodeProof(proof: Uint8Array): any {
            const free_buffers: number[] = [];
            const ptr = copy_to_c(proof, c4w, free_buffers);
            const resultPtr = c4w._c4w_decode_proof(ptr, proof.length);
            free_buffers.forEach(p => c4w._free(p));
            if (!resultPtr) throw new Error('Unknown proof format');
            return as_json(resultPtr, c4w, true);
        },

        registerStorage(storage: Storage) {
            c4w.storage = storage;
        },
    };
}

let runtime: Promise<C4Runtime> | null = null;

/**
 * Returns the (cached) WASM-backed runtime.
 * @return Initialized WASM runtime
 */
export function getWasmRuntime(): Promise<C4Runtime> {
    if (!runtime) runtime = getC4w().then(createRuntime);
    return runtime;
}
