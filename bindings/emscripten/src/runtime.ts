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

import type { DataRequest } from './types.js';
import type { Storage } from './wasm_shared.js';

export type { Storage };

/**
 * Opaque handle for a native/WASM context (prover, verifier or rpc).
 * WASM uses heap pointers (numbers), the native addon uses externals.
 */
export type CtxHandle = unknown;

/**
 * Parsed JSON status as returned by the C status-builder functions.
 * For the prover, `result` is materialized as `Uint8Array` (the proof).
 * For verify/rpc, `result` is the parsed JSON result value.
 */
export interface RuntimeStatus {
    status: 'success' | 'error' | 'pending' | 'revert';
    result?: any;
    error?: string;
    /** Revert data (EIP-3668 / CCIP-Read) when status is 'revert'. */
    data?: any;
    requests?: DataRequest[];
}

/**
 * Value-level abstraction over the C core, implemented by the WASM build
 * (`runtime_wasm.ts`) and the native Node.js addon (`runtime_node.ts`).
 *
 * All methods are synchronous: implementations are fully initialized when
 * returned by `getRuntime()`.
 */
export interface C4Runtime {
    readonly kind: 'wasm' | 'native';

    getMethodType(chainId: bigint, method: string, paramsJson: string | null, flags: number): number;

    createProverCtx(method: string, argsJson: string, chainId: bigint, flags: number): CtxHandle;
    /** Executes one prover step. On success, `result` contains the proof bytes. */
    executeProverCtx(ctx: CtxHandle): RuntimeStatus;
    freeProverCtx(ctx: CtxHandle): void;

    createVerifyCtx(
        proof: Uint8Array,
        method: string,
        argsJson: string,
        chainId: bigint,
        trustedCheckpoint: string | null,
        witnessKeys: string | null,
        flags: number,
        minLatestBlockTs: bigint
    ): CtxHandle;
    verifyProof(ctx: CtxHandle): RuntimeStatus;
    freeVerifyCtx(ctx: CtxHandle): void;

    createRpcCtx(method: string, paramsJson: string, chainId: bigint, proverFlags: number, verifyFlags: number, proverMode: number): CtxHandle;
    executeRpcCtx(ctx: CtxHandle): RuntimeStatus;
    freeRpcCtx(ctx: CtxHandle): void;
    rpcCtxSetProxyUrls(ctx: CtxHandle, rpcUrls: string, beaconUrls: string): void;
    rpcCtxSetWitnessKeys(ctx: CtxHandle, keys: string): void;
    rpcCtxSetMinLatestBlockTs(ctx: CtxHandle, ts: bigint): void;

    setCheckpoint(chainId: bigint, checkpoint: string): void;

    reqSetResponse(req: DataRequest, data: Uint8Array, nodeIndex: number): void;
    reqSetError(req: DataRequest, error: string, nodeIndex: number): void;

    decodeProof(proof: Uint8Array): any;
    registerStorage(storage: Storage): void;
}

export type RuntimeProvider = () => Promise<C4Runtime>;

// The active provider defaults to the WASM runtime and is replaced by the
// Node entry point (`index.node.ts`) with the native-first provider.
// Lazy import keeps browser bundles free of unused code paths.
let activeProvider: RuntimeProvider = async () => (await import('./runtime_wasm.js')).getWasmRuntime();
let cached: Promise<C4Runtime> | null = null;

/**
 * Replaces the runtime provider (e.g. native addon in Node.js).
 *
 * Intended to be called once by the entry point before the first
 * `getRuntime()` use. Calling it later resets the cached runtime, so the
 * next `getRuntime()` initializes the new provider - context handles
 * created by the previous runtime must not be used afterwards.
 * @param provider Factory returning an initialized runtime
 */
export function setRuntimeProvider(provider: RuntimeProvider) {
    activeProvider = provider;
    cached = null;
}

/**
 * Returns the active runtime (initialized lazily, cached afterwards).
 * @return The active C4Runtime instance
 */
export function getRuntime(): Promise<C4Runtime> {
    if (!cached) cached = activeProvider();
    return cached;
}
