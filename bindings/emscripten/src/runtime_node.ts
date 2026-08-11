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

import type { C4Runtime, CtxHandle, RuntimeStatus, Storage } from './runtime.js';
import type { DataRequest } from './types.js';
import { get_default_storage } from './wasm_shared.js';
import { module_dir } from './wasm.js';

/**
 * Node.js runtime: loads the native N-API addon (statically linked C library
 * with platform-optimized blst assembly) and falls back to the WASM runtime
 * when no prebuild exists for the current platform.
 *
 * Environment variables:
 * - `C4_NATIVE_ADDON`: absolute path to a `colibri_native.node` (overrides prebuild lookup)
 * - `C4_FORCE_NATIVE=1`: fail instead of falling back to WASM
 * - `C4_DISABLE_NATIVE=1`: skip the native addon entirely (always WASM)
 * - `C4_DEBUG_NATIVE=1`: log the reason when falling back to WASM
 */

type NativeBinding = {
    getMethodType(chainId: bigint, method: string, paramsJson: string | null, flags: number): number;

    createProverCtx(method: string, argsJson: string, chainId: bigint, flags: number): unknown;
    executeProverCtx(ctx: unknown): string;
    getProof(ctx: unknown): Uint8Array;
    freeProverCtx(ctx: unknown): void;

    createVerifyCtx(proof: Uint8Array, method: string, argsJson: string, chainId: bigint,
        trustedCheckpoint: string | null, witnessKeys: string | null,
        flags: number, minLatestBlockTs: bigint): unknown;
    verifyProof(ctx: unknown): string;
    freeVerifyCtx(ctx: unknown): void;

    createRpcCtx(method: string, paramsJson: string, chainId: bigint, proverFlags: number, verifyFlags: number, proverMode: number): unknown;
    executeRpcCtx(ctx: unknown): string;
    freeRpcCtx(ctx: unknown): void;
    rpcCtxSetWitnessKeys(ctx: unknown, keys: string | null): void;
    rpcCtxSetProxyUrls(ctx: unknown, rpcUrls: string | null, beaconUrls: string | null): void;
    rpcCtxSetMinLatestBlockTs(ctx: unknown, ts: bigint): void;

    reqSetResponse(reqPtr: string, data: Uint8Array, nodeIndex: number): void;
    reqSetError(reqPtr: string, error: string, nodeIndex: number): void;

    setCheckpoint(chainId: bigint, checkpoint: string | null): void;
    decodeProof(data: Uint8Array): string | null;
    registerStorage(storage: Storage): void;
    version(): number;
};

async function loadNativeBinding(): Promise<NativeBinding> {
    const { createRequire } = await import('node:module');
    const { join, dirname } = await import('node:path');
    const fs = await import('node:fs');

    // Directory of the compiled JS: resolved by wasm.ts (ESM: import.meta.url)
    // or wasm_cjs.ts (CJS: __dirname), which are swapped at build time.
    const dir = await module_dir();
    const requireNative = createRequire(join(dir, 'noop.js'));

    const candidates: string[] = [];
    if (process.env.C4_NATIVE_ADDON) candidates.push(process.env.C4_NATIVE_ADDON);
    const target = `${process.platform}-${process.arch}`;
    // The CJS build lives in a `cjs/` subfolder; prebuilds sit at the package root.
    candidates.push(join(dir, 'prebuilds', target, 'colibri_native.node'));
    candidates.push(join(dirname(dir), 'prebuilds', target, 'colibri_native.node'));

    for (const candidate of candidates) {
        if (fs.existsSync(candidate)) return requireNative(candidate) as NativeBinding;
    }
    throw new Error(`no native colibri addon found for ${target} (checked: ${candidates.join(', ')})`);
}

function createNativeRuntime(binding: NativeBinding): C4Runtime {
    // Most methods are direct pass-throughs (N-API functions don't need `this`);
    // only status parsing and the req_ptr string conversion add logic.
    return {
        kind: 'native',

        getMethodType: binding.getMethodType,

        createProverCtx: binding.createProverCtx,
        executeProverCtx(ctx: CtxHandle): RuntimeStatus {
            const state: RuntimeStatus = JSON.parse(binding.executeProverCtx(ctx));
            // The JSON status carries the proof as native pointer (unusable in JS);
            // fetch the bytes through the dedicated accessor instead.
            if (state.status === 'success') state.result = binding.getProof(ctx);
            return state;
        },
        freeProverCtx: binding.freeProverCtx,

        createVerifyCtx: binding.createVerifyCtx,
        verifyProof: (ctx: CtxHandle) => JSON.parse(binding.verifyProof(ctx)),
        freeVerifyCtx: binding.freeVerifyCtx,

        createRpcCtx: binding.createRpcCtx,
        executeRpcCtx: (ctx: CtxHandle) => JSON.parse(binding.executeRpcCtx(ctx)),
        freeRpcCtx: binding.freeRpcCtx,
        rpcCtxSetProxyUrls: binding.rpcCtxSetProxyUrls,
        rpcCtxSetWitnessKeys: binding.rpcCtxSetWitnessKeys,
        rpcCtxSetMinLatestBlockTs: binding.rpcCtxSetMinLatestBlockTs,

        setCheckpoint: binding.setCheckpoint,

        reqSetResponse: (req: DataRequest, data: Uint8Array, nodeIndex: number) =>
            binding.reqSetResponse(String(req.req_ptr), data, nodeIndex),
        reqSetError: (req: DataRequest, error: string, nodeIndex: number) =>
            binding.reqSetError(String(req.req_ptr), error, nodeIndex),

        decodeProof(proof: Uint8Array): any {
            const json = binding.decodeProof(proof);
            if (!json) throw new Error('Unknown proof format');
            return JSON.parse(json);
        },

        registerStorage: binding.registerStorage,
    };
}

let runtime: Promise<C4Runtime> | null = null;

async function initNodeRuntime(): Promise<C4Runtime> {
    if (process.env.C4_DISABLE_NATIVE !== '1') {
        try {
            const binding = await loadNativeBinding();
            const rt = createNativeRuntime(binding);
            // Same default persistence behavior as the WASM runtime (fs-based in Node).
            rt.registerStorage(await get_default_storage());
            return rt;
        } catch (e) {
            if (process.env.C4_FORCE_NATIVE === '1') throw e;
            if (process.env.C4_DEBUG_NATIVE === '1')
                console.warn('colibri: native addon unavailable, falling back to WASM:', e);
        }
    }
    return (await import('./runtime_wasm.js')).getWasmRuntime();
}

/**
 * Returns the (cached) Node runtime: native addon if available, otherwise WASM.
 * @return Initialized runtime
 */
export function getNodeRuntime(): Promise<C4Runtime> {
    if (!runtime) runtime = initNodeRuntime();
    return runtime;
}
