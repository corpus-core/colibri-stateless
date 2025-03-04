import { getC4w } from "./wasm.js";
import Client, { Config, DataRequest, Cache } from "./index.js";
import * as http from 'http';
import * as fs from 'fs';

// ensure cache_dir
let cache_memory_limit = parseInt(process.env.CACHE_RAM_LIMIT || '100') // in MB
let cache_dir = process.env.CACHE_DIR || '.cache'
if (!fs.existsSync(cache_dir)) fs.mkdirSync(cache_dir, { recursive: true });

interface JsonRpcRequest {
    jsonrpc: string;
    method: string;
    params: any[];
    id: number | string;
}
interface CacheEntry {
    data: Uint8Array;
    timestamp: number;
}
function cache_key(req: DataRequest) {
    if (req.type == 'eth_rpc' && req.payload)
        return `rpc_${req.payload?.method}_${req.payload?.params?.join('_')}`;
    return req.url;
}
const cached_methods = ["eth_getBlockByNumber", "eth_getBlockByHash", "eth_getBlockReceipts"];
const memory_cache: Map<string, CacheEntry> = new Map();
function check_limit() {
    let total_size = 0;
    for (const [k, v] of memory_cache.entries()) {
        total_size += v.data.length;
    }
    while (total_size > cache_memory_limit * 1024 * 1024) {
        // find the oldest entry
        let oldest_k = null;
        let oldest_v = null;
        for (const [k, v] of memory_cache.entries()) {
            if (oldest_v == null || v.timestamp < oldest_v.timestamp) {
                oldest_k = k;
                oldest_v = v;
            }
        }
        if (oldest_k) memory_cache.delete(oldest_k);
    }
}
function is_timeout(key: string, entry?: CacheEntry) {
    if (key.endsWith('latest') || key.endsWith('head')) {
        if (!entry) return true;
        return entry.timestamp < Date.now() - 1000 * 6;
    }
    return false;
}
const cache: Cache = {
    cacheable: (req: DataRequest) => !!(
        req.encoding == "ssz" || (req.type == 'eth_rpc' && req.payload && cached_methods.includes(req.payload?.method))
    ),
    get: (req: DataRequest) => {
        const key = cache_key(req);
        let entry = memory_cache.get(key);
        if (is_timeout(key, entry)) entry = undefined;
        if (!entry && !is_timeout(key)) {
            const file_path = cache_dir + '/' + key;
            if (fs.existsSync(file_path)) {
                let data = fs.readFileSync(file_path);
                memory_cache.set(key, { data, timestamp: fs.statSync(file_path).mtime.getTime() });
                setTimeout(check_limit, 0)
            }
        }
        return entry?.data;
    },
    set: (req: DataRequest, data: Uint8Array) => {
        const key = cache_key(req);
        memory_cache.set(key, { data, timestamp: Date.now() });
        if (!is_timeout(key)) fs.writeFile(cache_dir + '/' + key, Buffer.from(data), () => { });
        check_limit();
    }
};

export default class Server {
    client: Client;
    constructor(conf: Partial<Config>) {
        conf = conf || {};
        if (process.env.CHAIN_ID) conf.chainId = parseInt(process.env.CHAIN_ID);
        if (process.env.BEACON_API) conf.beacon_apis = process.env.BEACON_API.split(",");
        if (process.env.RPC) conf.rpcs = process.env.RPC.split(",");
        conf.cache = cache;
        conf.debug = true;
        this.client = new Client(conf);
    }

    private toHexString(bytes: Uint8Array): string {
        return '0x' + Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join('');
    }

    private async handleRequest(req: http.IncomingMessage, res: http.ServerResponse<http.IncomingMessage>) {
        // Only accept POST requests
        if (req.method !== 'POST') {
            const error = 'Method not allowed';
            console.error(`[${new Date().toISOString()}] Error: ${error}`);
            res.writeHead(405, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ error }));
            return;
        }

        // Read request body
        let body = '';
        req.on('data', (chunk: Buffer) => {
            body += chunk.toString();
        });

        req.on('end', async () => {
            try {
                const jsonRpc = JSON.parse(body) as JsonRpcRequest;
                if (!jsonRpc.method || !jsonRpc.params || !Array.isArray(jsonRpc.params)) {
                    throw new Error('Invalid JSON-RPC request');
                }
                let start = Date.now();
                console.log(` ==> ${jsonRpc.method} (${jsonRpc.params.join(',')}) ...`);


                // Create proof using the client
                const proof = await this.client.createProof(jsonRpc.method, jsonRpc.params);
                let end = Date.now();
                console.log(` <== ${jsonRpc.method} (${jsonRpc.params.join(',')}) ... ${end - start}ms (${proof.length} bytes)`);

                // Check Accept header to determine response format
                const acceptHeader = req.headers.accept;
                if (acceptHeader === 'application/octet-stream') {
                    res.writeHead(200, {
                        'Content-Type': 'application/octet-stream',
                        'Content-Length': proof.length
                    });
                    res.end(Buffer.from(proof));
                } else {
                    const response = {
                        jsonrpc: '2.0',
                        id: jsonRpc.id,
                        result: this.toHexString(proof)
                    };
                    res.writeHead(200, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify(response));
                }
            } catch (error: unknown) {
                const errorMessage = error instanceof Error ? error.message : 'Internal error';
                console.error(`[${new Date().toISOString()}] Error processing request:`, errorMessage);
                if (error instanceof Error && error.stack) {
                    console.error(`Stack trace:`, error.stack);
                }

                res.writeHead(400, { 'Content-Type': 'application/json' });
                const errorResponse = {
                    jsonrpc: '2.0',
                    id: (JSON.parse(body) as Partial<JsonRpcRequest>)?.id,
                    error: {
                        code: -32603,
                        message: errorMessage
                    }
                };
                res.end(JSON.stringify(errorResponse));
                console.log(`[${new Date().toISOString()}] Sent error response:`, errorResponse);
            }
        });

        req.on('error', (error) => {
            console.error(`[${new Date().toISOString()}] Request error:`, error);
            res.writeHead(500, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({
                jsonrpc: '2.0',
                error: {
                    code: -32603,
                    message: 'Internal server error'
                }
            }));
        });
    }

    async start(port: number = 8545) {
        const server = http.createServer((req, res) => this.handleRequest(req, res));
        return new Promise<void>((resolve, reject) => {
            server.listen(port, () => {
                console.log(`[${new Date().toISOString()}] Server running at http://localhost:${port}/`);
                resolve();
            });
            server.on('error', (error) => {
                console.error(`[${new Date().toISOString()}] Server error:`, error);
                reject(error);
            });
        });
    }
}

// Start the server directly
const server = new Server({});
server.start(parseInt(process.env.PORT || '8545'));
