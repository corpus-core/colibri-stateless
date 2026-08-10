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

export type LogLevel = 'trace' | 'debug' | 'info' | 'warn' | 'error';

export const DEFAULT_GATEWAY = 'https://tor-js-gateway.voltrevo.com';

export interface ThorBrowserOptions {
    /** WebSocket/WebRTC gateway URL for Tor relay connections.
     *  Default: `'https://tor-js-gateway.voltrevo.com'`. */
    gateway?: string;
    /** Callback invoked when Tor bootstrap completes, with elapsed time in ms. */
    onBootstrap?: (elapsedMs: number) => void;
    /** Arti log level. Default: `'warn'`. */
    logLevel?: LogLevel;
}

export interface ThorNodeOptions {
    /** SOCKS5 proxy hostname. Default: `'127.0.0.1'`. */
    socksHost?: string;
    /** SOCKS5 proxy port. Default: `9050`. */
    socksPort?: number;
}
