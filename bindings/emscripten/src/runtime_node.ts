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
import { createWasmRuntime } from './runtime_wasm.js';

type NativeBinding = {
    getMethodSupport(chainId: bigint, method: string): number;

    createProverCtx(method: string, paramsJson: string, chainId: bigint, flags: number): any;
    proverExecuteJsonStatus(ctx: any): string;
    proverGetProof(ctx: any): Uint8Array;
    freeProverCtx(ctx: any): void;

    createVerifyCtx(
        proof: Uint8Array,
        method: string,
        argsJson: string,
        chainId: bigint,
        trustedCheckpoint?: string | null,
        witnessKeys?: string | null
    ): any;
    verifyExecuteJsonStatus(ctx: any): string;
    freeVerifyCtx(ctx: any): void;

    reqSetResponse(reqPtr: number, data: Uint8Array, nodeIndex: number): void;
    reqSetError(reqPtr: number, error: string, nodeIndex: number): void;
};

function normalizeRequests(requests: any[] | undefined): DataRequest[] {
    if (!requests) return [];
    return requests.map((r: any) => ({
        ...r,
        exclude_mask: typeof r.exclude_mask === 'string' ? Number(r.exclude_mask) : r.exclude_mask,
    }));
}

async function loadNativeBinding(): Promise<NativeBinding> {
    const { createRequire } = await import('module');
    const { fileURLToPath, pathToFileURL } = await import('url');

    const pkgRoot = fileURLToPath(new URL('.', import.meta.url));

    // In a real npm install, this file lives under node_modules and normal resolution works.
    // During local development, we import from a build folder which has no node_modules.
    // Fall back to resolving from process.cwd() to find the dev dependency.
    const requireFromHere = createRequire(import.meta.url);
    const requireFromCwd = createRequire(pathToFileURL(process.cwd() + '/'));

    let nodeGypBuild: ((dir: string) => any) | null = null;
    try {
        nodeGypBuild = requireFromHere('node-gyp-build') as (dir: string) => any;
    } catch {
        nodeGypBuild = requireFromCwd('node-gyp-build') as (dir: string) => any;
    }

    return nodeGypBuild(pkgRoot) as NativeBinding;
}

function hexFromBytes(bytes: Uint8Array): string {
    let out = '';
    for (const b of bytes) out += b.toString(16).padStart(2, '0');
    return out;
}

async function readFileIfExists(path: string): Promise<Uint8Array | null> {
    try {
        const fs = await import('fs');
        return fs.readFileSync(path);
    } catch {
        return null;
    }
}

export function createNodeRuntime(): C4Runtime {
    const wasmFallback = createWasmRuntime();
    let bindingPromise: Promise<NativeBinding> | null = null;
    let nativeUnavailable = false;
    const forceNative = typeof process !== 'undefined' && process.env && process.env.C4W_FORCE_NATIVE === '1';

    async function getBindingOrNull(): Promise<NativeBinding | null> {
        if (nativeUnavailable) return null;
        if (!bindingPromise) bindingPromise = loadNativeBinding();
        try {
            return await bindingPromise;
        } catch {
            if (forceNative) {
                throw new Error('C4W_FORCE_NATIVE=1 but native addon could not be loaded');
            }
            nativeUnavailable = true;
            return null;
        }
    }

    return {
        async getMethodSupport(chainId: bigint, method: string): Promise<number> {
            const binding = await getBindingOrNull();
            if (!binding) return wasmFallback.getMethodSupport(chainId, method);
            return binding.getMethodSupport(chainId, method);
        },

        async createProverCtx(method: string, paramsJson: string, chainId: bigint, flags: number): Promise<ProverHandle> {
            const binding = await getBindingOrNull();
            if (!binding) return wasmFallback.createProverCtx(method, paramsJson, chainId, flags);
            return binding.createProverCtx(method, paramsJson, chainId, flags) as ProverHandle;
        },

        async proverStep(ctx: ProverHandle): Promise<ProverStepState> {
            const binding = await getBindingOrNull();
            if (!binding) return wasmFallback.proverStep(ctx);
            const status = JSON.parse(binding.proverExecuteJsonStatus(ctx));
            switch (status.status) {
                case 'success':
                    return { status: 'success', proof: binding.proverGetProof(ctx) };
                case 'error':
                    return { status: 'error', error: status.error };
                case 'pending':
                    return { status: 'waiting', requests: normalizeRequests(status.requests) };
                default:
                    return { status: 'error', error: `Unknown status: ${String(status.status)}` };
            }
        },

        async freeProverCtx(ctx: ProverHandle): Promise<void> {
            const binding = await getBindingOrNull();
            if (!binding) return wasmFallback.freeProverCtx(ctx);
            if (ctx) binding.freeProverCtx(ctx);
        },

        async createVerifyCtx(
            proof: Uint8Array,
            method: string,
            argsJson: string,
            chainId: bigint,
            trustedCheckpoint?: string | null,
            witnessKeys?: string | null
        ): Promise<VerifyHandle> {
            const binding = await getBindingOrNull();
            if (!binding) return wasmFallback.createVerifyCtx(proof, method, argsJson, chainId, trustedCheckpoint, witnessKeys);
            return binding.createVerifyCtx(proof, method, argsJson, chainId, trustedCheckpoint || null, witnessKeys || null) as VerifyHandle;
        },

        async verifyStep(ctx: VerifyHandle): Promise<VerifyStepState> {
            const binding = await getBindingOrNull();
            if (!binding) return wasmFallback.verifyStep(ctx);
            const status = JSON.parse(binding.verifyExecuteJsonStatus(ctx));
            switch (status.status) {
                case 'success':
                    return { status: 'success', result: status.result };
                case 'error':
                    return { status: 'error', error: status.error };
                case 'pending':
                    return { status: 'waiting', requests: normalizeRequests(status.requests) };
                default:
                    return { status: 'error', error: `Unknown status: ${String(status.status)}` };
            }
        },

        async freeVerifyCtx(ctx: VerifyHandle): Promise<void> {
            const binding = await getBindingOrNull();
            if (!binding) return wasmFallback.freeVerifyCtx(ctx);
            if (ctx) binding.freeVerifyCtx(ctx);
        },

        async reqSetResponse(reqPtr: ReqHandle, data: Uint8Array, nodeIndex: number): Promise<void> {
            const binding = await getBindingOrNull();
            if (!binding) return wasmFallback.reqSetResponse(reqPtr, data, nodeIndex);
            binding.reqSetResponse(Number(reqPtr), data, nodeIndex);
        },

        async reqSetError(reqPtr: ReqHandle, error: string, nodeIndex: number): Promise<void> {
            const binding = await getBindingOrNull();
            if (!binding) return wasmFallback.reqSetError(reqPtr, error, nodeIndex);
            binding.reqSetError(Number(reqPtr), error, nodeIndex);
        },

        async getProverConfigHex(chainId: number): Promise<string> {
            // Native runtime uses file storage. Try to read persisted sync state from disk.
            const dir = (typeof process !== 'undefined' && process.env && process.env.C4_STATE_DIR) ? process.env.C4_STATE_DIR : '.';
            const path = `${dir}/states_${chainId}`;
            const data = await readFileIfExists(path);
            if (data && data.length) return '0x' + hexFromBytes(data);
            // If no persisted state is available, fall back to the WASM runtime's embedded defaults.
            // This keeps behavior consistent across runtimes and avoids fixture drift in tests.
            return wasmFallback.getProverConfigHex(chainId);
        },

        async setTrustedCheckpoint(chainId: number, checkpoint: string): Promise<void> {
            // Native runtime passes trusted checkpoint into createVerifyCtx, so no global state is required here.
            void chainId;
            void checkpoint;
        },
    };
}


