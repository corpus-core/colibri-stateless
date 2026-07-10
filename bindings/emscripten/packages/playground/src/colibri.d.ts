// Minimal ambient typings for the locally-built (aliased) colibri-stateless
// browser bundle, which is not present as a typed npm package in this checkout.
declare module '@corpus-core/colibri-stateless' {
    export interface C4Config {
        chainId?: number;
        rpcs?: string[];
        prover?: string[];
        beacon_apis?: string[];
        checkpointz?: string[];
        /** Pragmatic Adaptive Privacy mode. "basic" enables the PAP verify flag. */
        privacy_mode?: 'none' | 'basic';
        /** Proof generation mode. "hybrid" combines local + remote proving. */
        prover_mode?: 'local' | 'remote' | 'hybrid' | 'proxy' | 'light_client';
        /**
         * TEE RPC endpoints that terminate the privacy-critical eth_getProof
         * requests inside a trusted enclave (ORAM-backed). When non-empty this
         * enables the oblivious verify flag (and PAP), so even the requested
         * account/storage keys are not leaked to the prover. One entry is enough.
         */
        oblivious_nodes?: string[];
        /** Request ZK-verified state proofs from the prover. */
        zk_proof?: boolean;
        /** Skip the Weak Subjectivity Period check (needed for older periods). */
        skip_wsp_check?: boolean;
        [key: string]: unknown;
    }

    export default class C4Client {
        constructor(config?: C4Config);
        rpc(method: string, args: unknown[], methodType?: number): Promise<unknown>;
        request(args: { method: string; params?: unknown[] }): Promise<unknown>;
    }

    export function set_wasm_url(url: string): void;
}
