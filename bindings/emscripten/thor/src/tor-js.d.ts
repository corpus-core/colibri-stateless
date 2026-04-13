/**
 * Ambient module declaration for tor-js.
 *
 * tor-js is loaded dynamically at runtime and may not be installed.
 * This declaration provides type information for the subset of the
 * tor-js API used by colibri-thor. Once tor-js is published on npm
 * with its own type declarations, this file can be removed.
 */
declare module 'tor-js' {
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
