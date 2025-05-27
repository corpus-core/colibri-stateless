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

    static createError(error: any): ProviderRpcError {
        let providerError: ProviderRpcError;
        if (error instanceof ProviderRpcError) {
            providerError = error;
        } else {
            let code: number;
            let message = error?.message || 'An unknown error occurred';
            const originalData = error?.data;
            if (typeof message === 'string' && message.startsWith('Method ') && message.endsWith(' is not supported')) {
                code = 4200;
            } else if (typeof message === 'string' && (message.includes('HTTP error!') || message.includes('All nodes failed') || message.includes('Failed to fetch'))) {
                code = 4900;
            } else {
                code = (typeof error?.code === 'number') ? error.code : -32603;
            }
            providerError = new ProviderRpcError(code, message, originalData);
        }

        return providerError;
    }

}

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

// C4Client configuration
export interface Config {
    chainId: number | string;
    beacon_apis: string[];
    rpcs: string[];
    proofer?: string[];
    trusted_block_hashes: string[];
    cache?: Cache;
    debug?: boolean;
    include_code?: boolean;
    verify?: (method: string, args: any[]) => boolean;
    pollingInterval?: number;
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