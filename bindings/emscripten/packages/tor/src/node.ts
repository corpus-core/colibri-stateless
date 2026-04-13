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

import type { ThorNodeOptions } from './types.js';
export type { ThorNodeOptions };

const SOCKS5_HANDSHAKE_TIMEOUT_MS = 30_000;
const HTTP_RESPONSE_TIMEOUT_MS = 60_000;
const MAX_RESPONSE_SIZE = 64 * 1024 * 1024; // 64 MiB

/**
 * Perform a SOCKS5 CONNECT handshake over an existing TCP socket.
 *
 * Implements RFC 1928 (no-auth) + CONNECT to a domain:port target.
 * Uses buffered reads to handle TCP stream fragmentation correctly.
 * The socket is left connected to the remote host on success.
 */
async function socks5Connect(
    socksHost: string,
    socksPort: number,
    targetHost: string,
    targetPort: number
): Promise<import('node:net').Socket> {
    const net = await import('node:net');

    const hostBuf = Buffer.from(targetHost, 'utf-8');
    if (hostBuf.length > 255) {
        throw new Error(`SOCKS5: hostname too long (${hostBuf.length} bytes, max 255)`);
    }

    return new Promise((resolve, reject) => {
        const socket = net.createConnection(socksPort, socksHost, () => {
            socket.write(Buffer.from([0x05, 0x01, 0x00]));
        });

        socket.setTimeout(SOCKS5_HANDSHAKE_TIMEOUT_MS);
        socket.once('timeout', () => {
            socket.destroy();
            reject(new Error('SOCKS5 handshake timeout'));
        });

        let phase: 'greeting' | 'connect' = 'greeting';
        let buffer = Buffer.alloc(0);

        socket.once('error', (err) => {
            socket.destroy();
            reject(err);
        });

        socket.on('data', (chunk: Buffer) => {
            buffer = Buffer.concat([buffer, chunk]);

            if (phase === 'greeting') {
                if (buffer.length < 2) return;
                if (buffer[0] !== 0x05 || buffer[1] !== 0x00) {
                    socket.destroy();
                    reject(new Error(`SOCKS5 handshake failed: server chose method ${buffer[1]}`));
                    return;
                }
                buffer = buffer.subarray(2);
                phase = 'connect';

                const req = Buffer.alloc(4 + 1 + hostBuf.length + 2);
                req[0] = 0x05; // VER
                req[1] = 0x01; // CMD: CONNECT
                req[2] = 0x00; // RSV
                req[3] = 0x03; // ATYP: DOMAINNAME
                req[4] = hostBuf.length;
                hostBuf.copy(req, 5);
                req.writeUInt16BE(targetPort, 5 + hostBuf.length);
                socket.write(req);
            }

            if (phase === 'connect') {
                // Minimum CONNECT response: VER(1) + REP(1) + RSV(1) + ATYP(1) + addr + port(2)
                // For ATYP=1 (IPv4): 4+4+2 = 10 bytes total
                if (buffer.length < 4) return;
                let expectedLen: number;
                switch (buffer[3]) {
                    case 0x01: expectedLen = 10; break;    // IPv4: 4 header + 4 addr + 2 port
                    case 0x04: expectedLen = 22; break;    // IPv6: 4 header + 16 addr + 2 port
                    case 0x03: {                           // Domain: 4 header + 1 len + N + 2 port
                        if (buffer.length < 5) return;
                        expectedLen = 5 + buffer[4] + 2;
                        break;
                    }
                    default: expectedLen = 10; break;
                }
                if (buffer.length < expectedLen) return;

                if (buffer[0] !== 0x05) {
                    socket.destroy();
                    reject(new Error('SOCKS5 connect: unexpected version'));
                    return;
                }
                if (buffer[1] !== 0x00) {
                    socket.destroy();
                    const codes: Record<number, string> = {
                        0x01: 'general failure',
                        0x02: 'connection not allowed',
                        0x03: 'network unreachable',
                        0x04: 'host unreachable',
                        0x05: 'connection refused',
                        0x06: 'TTL expired',
                        0x07: 'command not supported',
                        0x08: 'address type not supported',
                    };
                    reject(new Error(`SOCKS5 connect failed: ${codes[buffer[1]] || `code ${buffer[1]}`}`));
                    return;
                }
                socket.setTimeout(0);
                socket.removeAllListeners('data');
                socket.removeAllListeners('error');
                socket.removeAllListeners('timeout');
                resolve(socket);
            }
        });
    });
}

/**
 * Perform an HTTP request through an established SOCKS5 tunnel.
 *
 * For HTTPS targets, wraps the raw socket in TLS using `node:tls`.
 * Uses HTTP/1.0 to avoid chunked transfer-encoding complexity.
 * Returns a standard Web API `Response` (Node 18+).
 */
async function httpOverSocks(
    socksHost: string,
    socksPort: number,
    url: string,
    init?: RequestInit
): Promise<Response> {
    const parsed = new URL(url);
    const isHttps = parsed.protocol === 'https:';
    const targetPort = parsed.port ? parseInt(parsed.port) : (isHttps ? 443 : 80);

    let socket = await socks5Connect(socksHost, socksPort, parsed.hostname, targetPort);

    if (isHttps) {
        const tls = await import('node:tls');
        const tlsSocket = tls.connect({
            socket: socket,
            servername: parsed.hostname,
        });
        await new Promise<void>((resolve, reject) => {
            tlsSocket.once('secureConnect', resolve);
            tlsSocket.once('error', reject);
        });
        socket = tlsSocket as unknown as import('node:net').Socket;
    }

    const method = init?.method ?? 'GET';
    if (!/^[A-Z]+$/.test(method)) {
        socket.destroy();
        throw new Error(`Invalid HTTP method: ${method}`);
    }

    const bodyStr = init?.body != null
        ? (typeof init.body === 'string' ? init.body : new TextDecoder().decode(init.body as ArrayBuffer))
        : undefined;
    const bodyBuf = bodyStr ? Buffer.from(bodyStr, 'utf-8') : undefined;

    const headers: Record<string, string> = {
        'Host': parsed.host,
        'Connection': 'close',
    };
    if (init?.headers) {
        const h = init.headers;
        if (h instanceof Headers) {
            h.forEach((v, k) => { headers[k] = v; });
        } else if (Array.isArray(h)) {
            for (const [k, v] of h) headers[k] = v;
        } else {
            Object.assign(headers, h);
        }
    }
    if (bodyBuf) {
        headers['Content-Length'] = bodyBuf.length.toString();
    }

    // Validate headers against CRLF injection (CWE-113)
    for (const [k, v] of Object.entries(headers)) {
        if (/[\r\n]/.test(k) || /[\r\n]/.test(v)) {
            socket.destroy();
            throw new Error('Invalid header: CR/LF characters not allowed in header name or value');
        }
    }

    // HTTP/1.0 avoids chunked transfer-encoding; Connection: close ensures
    // the server closes the socket after the response.
    const path = parsed.pathname + parsed.search;
    let reqStr = `${method} ${path} HTTP/1.0\r\n`;
    for (const [k, v] of Object.entries(headers)) {
        reqStr += `${k}: ${v}\r\n`;
    }
    reqStr += '\r\n';

    return new Promise<Response>((resolve, reject) => {
        socket.setTimeout(HTTP_RESPONSE_TIMEOUT_MS);
        socket.once('timeout', () => {
            socket.destroy();
            reject(new Error('HTTP response timeout'));
        });
        socket.once('error', (err) => {
            socket.destroy();
            reject(err);
        });

        let totalSize = 0;
        const chunks: Buffer[] = [];
        socket.on('data', (chunk: Buffer) => {
            totalSize += chunk.length;
            if (totalSize > MAX_RESPONSE_SIZE) {
                socket.destroy();
                reject(new Error(`Response too large (>${MAX_RESPONSE_SIZE} bytes)`));
                return;
            }
            chunks.push(chunk);
        });
        socket.on('end', () => {
            socket.destroy();
            try {
                const raw = Buffer.concat(chunks);
                const headerEnd = raw.indexOf('\r\n\r\n');
                if (headerEnd === -1) {
                    reject(new Error('Malformed HTTP response: no header/body separator'));
                    return;
                }

                const headerStr = raw.subarray(0, headerEnd).toString('utf-8');
                const body = raw.subarray(headerEnd + 4);

                const [statusLine, ...headerLines] = headerStr.split('\r\n');
                const statusMatch = statusLine.match(/^HTTP\/[\d.]+ (\d+)/);
                const status = statusMatch ? parseInt(statusMatch[1]) : 0;

                const respHeaders = new Headers();
                for (const line of headerLines) {
                    const idx = line.indexOf(':');
                    if (idx > 0) {
                        respHeaders.append(line.substring(0, idx).trim(), line.substring(idx + 1).trim());
                    }
                }

                resolve(new Response(body, { status, headers: respHeaders }));
            } catch (e) {
                reject(e);
            }
        });

        socket.write(reqStr);
        if (bodyBuf) socket.write(bodyBuf);
    });
}

/**
 * Create a `fetch`-compatible function that routes HTTP requests through a
 * locally running Tor SOCKS5 proxy.  Node.js only.
 *
 * The user must start the Tor daemon themselves (e.g. `tor --SocksPort 9050`).
 *
 * @param options - Node.js transport options (SOCKS host/port)
 * @return A `fetch`-compatible function routing through the Tor SOCKS5 proxy
 */
export async function createSocksFetch(
    options: ThorNodeOptions = {}
): Promise<typeof globalThis.fetch> {
    const host = options.socksHost ?? '127.0.0.1';
    const port = options.socksPort ?? 9050;

    if (port < 1 || port > 65535 || !Number.isInteger(port)) {
        throw new Error(`Invalid SOCKS port: ${port} (must be 1-65535)`);
    }

    const socksFetch: typeof globalThis.fetch = async (
        input: RequestInfo | URL,
        init?: RequestInit
    ): Promise<Response> => {
        const url = typeof input === 'string'
            ? input
            : input instanceof URL
                ? input.href
                : input.url;

        return httpOverSocks(host, port, url, init);
    };

    return socksFetch;
}
