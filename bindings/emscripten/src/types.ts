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
    getMethodSupport(method: string): Promise<MethodType>;
}

export type FetchRpc = (urls: string[], payload: any, as_proof: boolean) => Promise<any>;
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

// Custom cache implementation
export interface Cache {
    cacheable(req: DataRequest): boolean;
    get(req: DataRequest): Uint8Array | undefined;
    set(req: DataRequest, data: Uint8Array): void;
}

export interface ChainConfig {
    beacon_apis: string[];
    rpcs: string[];
    prover?: string[];
    checkpointz?: string[];
    trusted_block_hashes: string[];
    verify?: (method: string, args: any[]) => boolean;
    pollingInterval?: number;
    proofStrategy?: ProofStrategy;
    verifyTransactions?: boolean; // Neue Option für Transaction-Verifikation
}

export interface EIP1193Client {
    request(args: RequestArguments): Promise<unknown>
    on(event: string, callback: (data: any) => void): this
    removeListener(event: string, callback: (data: any) => void): this
}


// C4Client configuration
export interface Config extends ChainConfig {
    chainId: number | string;
    cache?: Cache;
    debug?: boolean;
    include_code?: boolean;
    chains: {
        [chainId: number]: ChainConfig;
    };
    fallback_provider?: EIP1193Client;
    warningHandler: WarningHandler;
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
}

// Enum for RPC method types
export enum MethodType {
    PROOFABLE = 1,
    UNPROOFABLE = 2,
    NOT_SUPPORTED = 3,
    LOCAL = 4
} 