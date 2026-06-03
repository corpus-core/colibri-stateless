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

// --- EIP-1193 & C4Client Specific Types ---

// From EIP-1193: Error structure
export class ProviderRpcError extends Error {
    public code: number;
    public data?: unknown;

    constructor(code: number, message: string, data?: unknown) {
        super(message);
        this.name = 'ProviderRpcError';
        this.code = code;
        if (data !== undefined) {
            this.data = data;
        }
        Object.setPrototypeOf(this, ProviderRpcError.prototype);
    }

    static createError(error: any, args?: RequestArguments): ProviderRpcError {
        let providerError: ProviderRpcError;
        if (error instanceof ProviderRpcError) {
            providerError = error;
        } else {
            let code: number;
            let message = (error?.message || 'An unknown error occurred');
            const originalData = error?.data;
            if (typeof message === 'string' && message.startsWith('Method ') && message.endsWith(' is not supported')) {
                code = 4200;
            } else if (typeof message === 'string' && (message.includes('HTTP error!') || message.includes('All nodes failed') || message.includes('Failed to fetch'))) {
                code = 4900;
            } else {
                code = (typeof error?.code === 'number') ? error.code : -32603;
            }
            providerError = new ProviderRpcError(code, 'Error in rpc call ' + (args?.method || '') + JSON.stringify(args?.params || []) + ' : ' + message, originalData);
        }

        return providerError;
    }

}

export interface ColibriClient {
    rpc(method: string, params: any[], method_type?: MethodType): Promise<any>;
    getMethodSupport(method: string, args?: any[]): Promise<MethodType>;
}

export type FetchRpc = (urls: string[], payload: any, as_proof: boolean, fetchFn?: typeof globalThis.fetch) => Promise<any>;
export type ProofStrategy = (client: ColibriClient, req: RequestArguments, config: Config, fetch_rpc: FetchRpc) => Promise<any>;
export type WarningHandler = (req: RequestArguments, message: string) => Promise<any>;




// From EIP-1193: Request arguments
export interface RequestArguments {
    readonly method: string;
    readonly params?: readonly unknown[] | object;
}

// From EIP-1193: connect event payload
export interface ProviderConnectInfo {
    readonly chainId: string;
}

// From EIP-1193: message event payload
export interface ProviderMessage {
    readonly type: string;
    readonly data: unknown;
}

// C4Client specific types

/** Pragmatic Adaptive Privacy mode. PAP_BASIC sets VERIFY_FLAG_PAP for method-type and verification. */
export type PrivacyMode = 'none' | 'basic';

// Custom cache implementation
export interface Cache {
    cacheable(req: DataRequest): boolean;
    get(req: DataRequest): Uint8Array | undefined | null | Promise<Uint8Array | undefined | null>;
    set(req: DataRequest, data: Uint8Array): void;
}

export interface ChainConfig {
    beacon_apis: string[];
    rpcs: string[];
    /** TEE RPC endpoints for eth_getProof. Sets VERIFY_FLAG_OBLIVIOUS and PAP when non-empty. Use with privacy_mode "basic" and prover_mode "hybrid" for private eth_call. */
    oblivious_nodes?: string[];
    prover?: string[];
    checkpointz?: string[];
    trusted_checkpoint?: string;
    verify?: (method: string, args: any[]) => boolean;
    pollingInterval?: number;
    proofStrategy?: ProofStrategy;
    verifyTransactions?: boolean;
}

export interface EIP1193Client {
    request(args: RequestArguments): Promise<unknown>
    on(event: string, callback: (data: any) => void): this
    removeListener(event: string, callback: (data: any) => void): this
}


/** Proof generation mode controlling how proofs are built and verified. */
export type ProverMode = 'local' | 'remote' | 'hybrid' | 'proxy' | 'light_client';

// C4Client configuration
export interface Config extends ChainConfig {
    chainId: number | string;
    checkpoint_witness_keys?: string;
    cache?: Cache;
    debug?: boolean;
    include_code?: boolean;
    use_accesslist?: boolean;
    /** Pragmatic Adaptive Privacy mode. Default "none". "basic" sets verify flag for PAP. */
    privacy_mode?: PrivacyMode;
    zk_proof?: boolean;
    /** Proof generation mode. Default: "remote" if prover URLs configured, otherwise "local". */
    prover_mode?: ProverMode;
    /**
     * If true, the verifier skips the Weak Subjectivity Period check
     * (`VERIFY_FLAG_SKIP_WSP_CHECK`, bit `1 << 7`). **SECURITY:** only safe when another
     * trust anchor (witness signatures, hard-coded checkpoint, signed package) is in
     * place; disabling raises the risk of long-range attacks across periods older than
     * the WSP. Default: false.
     */
    skip_wsp_check?: boolean;
    /**
     * Maximum age (in seconds) accepted for a proof whose request uses the
     * `"latest"` block tag. The verifier compares the block timestamp from
     * the proof against `now - max_latest_age_seconds`; older proofs are
     * rejected with `"proof for latest too old"`. `0` disables the check
     * (useful when working with older proof formats that lack a block
     * context). Currently active for `eth_call`, `eth_estimateGas`, and
     * `colibri_simulateTransaction`. Default: 60.
     */
    max_latest_age_seconds?: number;
    chains: {
        [chainId: number]: ChainConfig;
    };
    fallback_provider?: EIP1193Client;
    warningHandler: WarningHandler;
    /** Optional callback invoked for every sub-request transfer with the response byte count. */
    onTransfer?: (size: number, req: DataRequest) => void;
    /** Custom fetch function replacing `globalThis.fetch` for all HTTP requests.
     *  Use this to route traffic through Tor, a SOCKS proxy, or any other transport layer. */
    fetch?: typeof globalThis.fetch;
}

// Data request structure used internally
export interface DataRequest {
    method: string;
    chain_id: number;
    encoding: string;
    type: string;
    exclude_mask: number;
    url: string;
    payload: any;
    req_ptr: number;
    /** Milliseconds to wait before (re-)executing this request (e.g. oblivious-node retry backoff). Optional. */
    delay?: number;
}

// Enum for RPC method types
export enum MethodType {
    PROOFABLE = 1,
    UNPROOFABLE = 2,
    NOT_SUPPORTED = 3,
    LOCAL = 4
} 