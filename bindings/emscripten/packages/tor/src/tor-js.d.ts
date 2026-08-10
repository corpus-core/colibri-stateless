/**
 * Ambient module declaration for the tor-js `wasm-base64` entry point.
 *
 * tor-js ships its own type declarations, but the `tsconfig` uses
 * `moduleResolution: "node"`, which does not resolve the package's
 * `exports` subpaths. This declaration provides type information for the
 * subset of the tor-js API used by colibri-tor.
 */
declare module 'tor-js/wasm-base64' {
    export interface TorClientOptions {
        gateway?: string;
        logLevel?: 'trace' | 'debug' | 'info' | 'warn' | 'error';
        storage?: unknown;
        socketProvider?: unknown;
    }

    export interface FetchInit {
        method?: string;
        headers?: Record<string, string>;
        body?: string | Uint8Array | ArrayBuffer;
        signal?: AbortSignal;
    }

    export class TorClient {
        constructor(options?: TorClientOptions);
        fetch(url: string, init?: FetchInit): Promise<Response>;
        ready(): Promise<void>;
        close(): void;
    }
}
