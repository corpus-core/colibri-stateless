// Minimal ambient typings for the locally-built (aliased) colibri-stateless
// browser bundle, which is not present as a typed npm package in this checkout.
declare module '@corpus-core/colibri-stateless' {
    export interface C4Config {
        chainId?: number;
        rpcs?: string[];
        prover?: string[];
        beacon_apis?: string[];
        checkpointz?: string[];
        [key: string]: unknown;
    }

    export default class C4Client {
        constructor(config?: C4Config);
        rpc(method: string, args: unknown[], methodType?: number): Promise<unknown>;
        request(args: { method: string; params?: unknown[] }): Promise<unknown>;
    }

    export function set_wasm_url(url: string): void;
}
