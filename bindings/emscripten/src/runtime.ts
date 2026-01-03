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

import { DataRequest } from './types.js';

export type ProverHandle = unknown;
export type VerifyHandle = unknown;
export type ReqHandle = unknown;

export type ProverStepState =
  | { status: 'success'; proof: Uint8Array }
  | { status: 'error'; error: string }
  | { status: 'waiting'; requests: DataRequest[] };

export type VerifyStepState =
  | { status: 'success'; result: any }
  | { status: 'error'; error: string }
  | { status: 'waiting'; requests: DataRequest[] };

/**
 * Runtime abstraction for the C core.
 *
 * - In browsers this is backed by Emscripten/WASM.
 * - In Node.js this is backed by a native N-API addon (preferred) with a WASM fallback.
 */
export interface C4Runtime {
  getMethodSupport(chainId: bigint, method: string): Promise<number>;

  createProverCtx(method: string, paramsJson: string, chainId: bigint, flags: number): Promise<ProverHandle>;
  proverStep(ctx: ProverHandle): Promise<ProverStepState>;
  freeProverCtx(ctx: ProverHandle): Promise<void>;

  createVerifyCtx(
    proof: Uint8Array,
    method: string,
    argsJson: string,
    chainId: bigint,
    trustedCheckpoint?: string | null,
    witnessKeys?: string | null
  ): Promise<VerifyHandle>;
  verifyStep(ctx: VerifyHandle): Promise<VerifyStepState>;
  freeVerifyCtx(ctx: VerifyHandle): Promise<void>;

  reqSetResponse(reqPtr: ReqHandle, data: Uint8Array, nodeIndex: number): Promise<void>;
  reqSetError(reqPtr: ReqHandle, error: string, nodeIndex: number): Promise<void>;

  getProverConfigHex(chainId: number): Promise<string>;
  setTrustedCheckpoint(chainId: number, checkpoint: string): Promise<void>;

  /**
   * Optional storage override hook. This is only supported in the WASM backend today.
   */
  registerStorage?: (storage: any) => Promise<void>;
}


