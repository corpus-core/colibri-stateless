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

import type { ThorBrowserOptions } from './types.js';
import { DEFAULT_GATEWAY } from './types.js';
export type { ThorBrowserOptions };
export { DEFAULT_GATEWAY };

// tor-js is loaded dynamically so the WASM payload is only pulled in when the
// browser transport is actually used.  The `wasm-base64` entry point embeds the
// Arti WASM directly in the JS bundle, so no CDN fetch is required at runtime.
let torJsImport: Promise<any> | null = null;

function getTorJs(): Promise<any> {
    if (!torJsImport) {
        torJsImport = import('tor-js/wasm-base64').catch(() => {
            throw new Error(
                'tor-js is not installed. Install it via `npm install tor-js`.'
            );
        });
    }
    return torJsImport;
}

/**
 * Create a `fetch`-compatible function that routes HTTP requests through the
 * Tor network using Arti compiled to WebAssembly.  Browser only.
 *
 * Tor bootstrap starts immediately but does **not** block the returned
 * function.  The first actual `fetch` call will await the bootstrap if it
 * has not completed yet.  This allows the application to continue
 * initializing while Tor connects in the background.
 *
 * ```typescript
 * // Bootstrap starts immediately, returns without blocking
 * const torFetch = createBrowserFetch();
 * const client = new Colibri({ fetch: torFetch });
 * // ... app continues initializing ...
 * // First request will await bootstrap completion if still in progress
 * await client.request({ method: 'eth_blockNumber' });
 * ```
 *
 * @param options - Browser transport options (gateway URL, log level, etc.)
 * @return A `fetch`-compatible function that routes through Tor
 */
export function createBrowserFetch(
    options: ThorBrowserOptions = {}
): typeof globalThis.fetch {
    const gateway = options.gateway ?? DEFAULT_GATEWAY;

    // Kick off bootstrap eagerly -- the promise is shared across all requests.
    const startTime = Date.now();
    const clientReady: Promise<any> = getTorJs().then(async ({ TorClient }) => {
        const client = new TorClient({
            gateway,
            logLevel: options.logLevel ?? 'warn',
        });
        await client.ready();
        options.onBootstrap?.(Date.now() - startTime);
        return client;
    });

    const torFetch: typeof globalThis.fetch = async (
        input: RequestInfo | URL,
        init?: RequestInit
    ): Promise<Response> => {
        const client = await clientReady;

        const url = typeof input === 'string'
            ? input
            : input instanceof URL
                ? input.href
                : input.url;

        // tor-js TorClient.fetch() returns a Response-compatible object.
        // We validate the essential properties to catch API mismatches early.
        const response = await client.fetch(url, {
            method: init?.method,
            headers: init?.headers as Record<string, string> | undefined,
            body: init?.body as string | Uint8Array | undefined,
        });

        if (typeof response?.status !== 'number') {
            throw new Error('tor-js returned an invalid response object (missing status)');
        }

        return response as Response;
    };

    return torFetch;
}
