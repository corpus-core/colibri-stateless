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

import type { C4Runtime, ProverHandle, ProverStepState, ReqHandle, VerifyHandle, VerifyStepState } from './runtime.js';
import type { DataRequest } from './types.js';
import {
    as_bytes,
    as_char_ptr,
    as_json,
    copy_to_c,
    getC4w,
    get_prover_config_hex,
    set_trusted_checkpoint
} from './wasm.js';

function normalizeRequests(requests: any[] | undefined): DataRequest[] {
    if (!requests) return [];
    return requests.map((r: any) => ({
        ...r,
        exclude_mask: typeof r.exclude_mask === 'string' ? Number(r.exclude_mask) : r.exclude_mask,
    }));
}

export function createWasmRuntime(): C4Runtime {
    return {
        async getMethodSupport(chainId: bigint, method: string): Promise<number> {
            const c4w = await getC4w();
            const free_buffers: number[] = [];
            const res = c4w._c4w_get_method_type(chainId, as_char_ptr(method, c4w, free_buffers));
            free_buffers.forEach(ptr => c4w._free(ptr));
            return res as number;
        },

        async createProverCtx(method: string, paramsJson: string, chainId: bigint, flags: number): Promise<ProverHandle> {
            const c4w = await getC4w();
            const free_buffers: number[] = [];
            try {
                const ctx = c4w._c4w_create_proof_ctx(
                    as_char_ptr(method, c4w, free_buffers),
                    as_char_ptr(paramsJson, c4w, free_buffers),
                    chainId,
                    flags
                );
                return ctx as unknown as ProverHandle;
            } finally {
                free_buffers.forEach(ptr => c4w._free(ptr));
            }
        },

        async proverStep(ctx: ProverHandle): Promise<ProverStepState> {
            const c4w = await getC4w();
            const state = as_json(c4w._c4w_execute_proof_ctx(ctx as any), c4w, true);
            switch (state.status) {
                case 'success':
                    return {
                        status: 'success',
                        // Do NOT free the proof pointer; it is owned by the context.
                        proof: as_bytes(state.result, state.result_len, c4w, false),
                    };
                case 'error':
                    return { status: 'error', error: state.error };
                case 'waiting':
                    return { status: 'waiting', requests: normalizeRequests(state.requests) };
                default:
                    return { status: 'error', error: `Unknown status: ${String(state.status)}` };
            }
        },

        async freeProverCtx(ctx: ProverHandle): Promise<void> {
            const c4w = await getC4w();
            if (ctx) c4w._c4w_free_proof_ctx(ctx as any);
        },

        async createVerifyCtx(
            proof: Uint8Array,
            method: string,
            argsJson: string,
            chainId: bigint,
            trustedCheckpoint?: string | null,
            witnessKeys?: string | null
        ): Promise<VerifyHandle> {
            const c4w = await getC4w();
            const free_buffers: number[] = [];
            const checkpoint_ptr = trustedCheckpoint ? as_char_ptr(trustedCheckpoint, c4w, free_buffers) : 0;
            const witness_keys_ptr = witnessKeys ? as_char_ptr(witnessKeys, c4w, free_buffers) : 0;
            try {
                const ctx = c4w._c4w_create_verify_ctx(
                    copy_to_c(proof, c4w, free_buffers),
                    proof.length,
                    as_char_ptr(method, c4w, free_buffers),
                    as_char_ptr(argsJson, c4w, free_buffers),
                    chainId,
                    checkpoint_ptr,
                    witness_keys_ptr
                );
                return ctx as unknown as VerifyHandle;
            } finally {
                free_buffers.forEach(ptr => c4w._free(ptr));
            }
        },

        async verifyStep(ctx: VerifyHandle): Promise<VerifyStepState> {
            const c4w = await getC4w();
            const state = as_json(c4w._c4w_verify_proof(ctx as any), c4w, true);
            switch (state.status) {
                case 'success':
                    return { status: 'success', result: state.result };
                case 'error':
                    return { status: 'error', error: state.error };
                case 'waiting':
                    return { status: 'waiting', requests: normalizeRequests(state.requests) };
                default:
                    return { status: 'error', error: `Unknown status: ${String(state.status)}` };
            }
        },

        async freeVerifyCtx(ctx: VerifyHandle): Promise<void> {
            const c4w = await getC4w();
            if (ctx) c4w._c4w_free_verify_ctx(ctx as any);
        },

        async reqSetResponse(reqPtr: ReqHandle, data: Uint8Array, nodeIndex: number): Promise<void> {
            const c4w = await getC4w();
            c4w._c4w_req_set_response(reqPtr as any, copy_to_c(data, c4w), data.length, nodeIndex);
        },

        async reqSetError(reqPtr: ReqHandle, error: string, nodeIndex: number): Promise<void> {
            const c4w = await getC4w();
            const free_buffers: number[] = [];
            try {
                c4w._c4w_req_set_error(reqPtr as any, as_char_ptr(error, c4w, free_buffers), nodeIndex);
            } finally {
                free_buffers.forEach(ptr => c4w._free(ptr));
            }
        },

        async getProverConfigHex(chainId: number): Promise<string> {
            return get_prover_config_hex(chainId);
        },

        async setTrustedCheckpoint(chainId: number, checkpoint: string): Promise<void> {
            return set_trusted_checkpoint(chainId, checkpoint);
        },

        async registerStorage(storage: any): Promise<void> {
            const c4w = await getC4w();
            c4w.storage = storage;
            c4w._init_storage();
        },
    };
}


