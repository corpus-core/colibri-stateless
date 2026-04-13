import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import net from 'node:net';

/**
 * Unit tests for the SOCKS5 handshake and createSocksFetch logic.
 *
 * These tests use a mock SOCKS5 server to verify protocol correctness
 * without requiring a real Tor daemon.
 */

describe('createSocksFetch', () => {

    it('should export createSocksFetch from the node entry point', async () => {
        // Verify the module structure is correct (import will fail at tsc
        // level if types are wrong, but we test the runtime export shape).
        const mod = await import('../dist/node.js');
        assert.equal(typeof mod.createSocksFetch, 'function');
    });

    it('should return a function with fetch-compatible signature', async () => {
        const { createSocksFetch } = await import('../dist/node.js');
        const fetchFn = await createSocksFetch({ socksPort: 19999 });
        assert.equal(typeof fetchFn, 'function');
    });

    it('should fail gracefully when SOCKS proxy is not reachable', async () => {
        const { createSocksFetch } = await import('../dist/node.js');
        // Port 1 is almost certainly not running a SOCKS proxy
        const fetchFn = await createSocksFetch({ socksPort: 1 });
        await assert.rejects(
            () => fetchFn('http://example.com'),
            (err) => err instanceof Error
        );
    });

    it('should perform correct SOCKS5 handshake with mock server', async () => {
        const handshakeLog = [];

        // Minimal SOCKS5 mock that accepts greeting + responds with success
        const server = net.createServer((socket) => {
            let phase = 'greeting';
            socket.on('data', (data) => {
                if (phase === 'greeting') {
                    handshakeLog.push('greeting');
                    // VER=5, NMETHODS=1, METHOD=0
                    assert.equal(data[0], 0x05);
                    assert.equal(data[1], 0x01);
                    assert.equal(data[2], 0x00);
                    // Accept no-auth
                    socket.write(Buffer.from([0x05, 0x00]));
                    phase = 'connect';
                } else if (phase === 'connect') {
                    handshakeLog.push('connect');
                    // VER=5, CMD=1(CONNECT), RSV=0, ATYP=3(DOMAIN)
                    assert.equal(data[0], 0x05);
                    assert.equal(data[1], 0x01);
                    assert.equal(data[3], 0x03);
                    // Respond with success + dummy bind address
                    const resp = Buffer.from([
                        0x05, 0x00, 0x00, 0x01,
                        0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00
                    ]);
                    socket.write(resp);
                    phase = 'http';
                } else {
                    // HTTP phase: respond with a simple HTTP response
                    const body = '{"ok":true}';
                    const httpResp = [
                        'HTTP/1.1 200 OK',
                        'Content-Type: application/json',
                        `Content-Length: ${body.length}`,
                        '',
                        body
                    ].join('\r\n');
                    socket.write(httpResp);
                    socket.end();
                }
            });
        });

        await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
        const port = server.address().port;

        try {
            const { createSocksFetch } = await import('../dist/node.js');
            const fetchFn = await createSocksFetch({ socksPort: port });
            // Use http:// to avoid TLS handshake with mock
            const response = await fetchFn('http://example.com/test');
            assert.equal(response.status, 200);
            const json = await response.json();
            assert.deepEqual(json, { ok: true });
            assert.deepEqual(handshakeLog, ['greeting', 'connect']);
        } finally {
            server.close();
        }
    });
});

describe('module exports', () => {
    it('should export all expected symbols from the main entry point', async () => {
        const mod = await import('../dist/index.js');
        assert.equal(typeof mod.createBrowserFetch, 'function');
        assert.equal(typeof mod.createSocksFetch, 'function');
    });
});
