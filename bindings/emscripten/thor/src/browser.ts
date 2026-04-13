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
export type { ThorBrowserOptions };

// tor-js is loaded dynamically so the import path can switch between vendored
// artifacts (built from source via scripts/build-arti.sh) and the future npm
// package without changing this file.
let torJsImport: Promise<any> | null = null;

function getTorJs(): Promise<any> {
    if (!torJsImport) {
        // Dynamic import -- resolved at runtime.  When tor-js is published on
        // npm this will become `import('tor-js')`.  Until then, the vendored
        // build (src/vendor/tor-js.js) is used via the package.json "imports"
        // map or a direct relative path.
        torJsImport = import('tor-js').catch(() => {
            throw new Error(
                'tor-js is not installed. Install it via npm (once published) ' +
                'or run `npm run build:arti` to build from source.'
            );
        });
    }
    return torJsImport;
}

/**
 * Create a `fetch`-compatible function that routes HTTP requests through the
 * Tor network using Arti compiled to WebAssembly.  Browser only.
 *
 * The returned function matches the `globalThis.fetch` signature and can be
 * passed directly to the Colibri client's `fetch` config option.
 *
 * @param options - Browser transport options (gateway URL, log level, etc.)
 * @return A `fetch`-compatible function routing through Tor
 */
export async function createBrowserFetch(
    options: ThorBrowserOptions = {}
): Promise<typeof globalThis.fetch> {
    const { TorClient } = await getTorJs();

    const startTime = Date.now();
    const client = new TorClient({
        gateway: options.gateway,
        logLevel: options.logLevel ?? 'warn',
    });
    await client.ready();
    options.onBootstrap?.(Date.now() - startTime);

    const torFetch: typeof globalThis.fetch = async (
        input: RequestInfo | URL,
        init?: RequestInit
    ): Promise<Response> => {
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
