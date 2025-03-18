// Import the Emscripten-generated module
import { getC4w, as_char_ptr, as_json, as_bytes, copy_to_c, Storage as C4Storage } from "./wasm.js";
export interface Cache {
    cacheable(req: DataRequest): boolean;
    get(req: DataRequest): Uint8Array | undefined;
    set(req: DataRequest, data: Uint8Array): void;
}
export interface Config {
    chainId: number;
    beacon_apis: string[],
    rpcs: string[];
    proofer?: string[];
    trusted_block_hashes: string[];
    cache?: Cache;
    debug?: boolean;
    include_code?: boolean;
    verify?: (method: string, args: any[]) => boolean;
}

export interface DataRequest {
    method: string;
    chain_id: number;
    encoding: string;
    type: string;
    exclude_mask: number;
    url: string;
    payload: any;
    req_ptr: number;
}

async function initialize_storage(conf: Config) {
    // TODO handle trusted_block_hashes
    const c4w = await getC4w();
}

async function fetch_rpc(urls: string[], method: string, params: any[], as_proof: boolean = false) {
    let last_error = "All nodes failed";
    for (const url of urls) {
        const response = await fetch(url, {
            method: 'POST',
            body: JSON.stringify({ id: 1, jsonrpc: "2.0", method, params }),
            headers: {
                "Content-Type": "application/json",
                "Accept": as_proof ? "application/octet-stream" : "application/json"
            }
        });
        if (response.ok) {
            if (!as_proof) {
                const res = await response.json();
                if (res.error) {
                    last_error = res.error?.message || res.error;
                    continue;
                }
                return res.result;
            }
            const bytes = await response.blob().then(blob => blob.arrayBuffer());
            return new Uint8Array(bytes);
        }
        else last_error = `HTTP error! Status: ${response.status}, Details: ${await response.text()}`;
    }
    throw new Error(last_error);
}
function log(msg: string) {
    console.error(msg);
}
export async function handle_request(req: DataRequest, conf: Config) {

    const free_buffers: number[] = [];
    const servers = req.type == "beacon_api" ? conf.beacon_apis : conf.rpcs;
    const c4w = await getC4w();
    let path = (req.type == 'eth_rpc' && req.payload)
        ? `rpc: ${req.payload?.method}(${req.payload?.params.join(',')})`
        : req.url;


    let cacheable = conf.cache && conf.cache.cacheable(req);

    if (cacheable && conf.cache) {
        const data = conf.cache.get(req);
        if (data) {
            if (conf.debug) log(`::: ${path} (len=${data.length} bytes) CACHED`);
            c4w._c4w_req_set_response(req.req_ptr, copy_to_c(data, c4w), data.length, 0);
            return;
        }
    }
    let node_index = 0;
    let last_error = "All nodes failed";
    for (const server of servers) {
        if (req.exclude_mask & (1 << node_index)) {
            node_index++;
            continue;
        }
        try {
            const response = await fetch(server + (req.url ? ('/' + req.url) : ''), {
                method: req.method,
                body: req.payload ? JSON.stringify(req.payload) : undefined,
                headers: {
                    "Content-Type": "application/json",
                    "Accept": req.encoding == "json" ? "application/json" : "application/octet-stream"
                }
            });

            if (!response.ok) throw new Error(`HTTP error! Status: ${response.status}, Details: ${await response.text()}`);

            const bytes = await response.blob().then(blob => blob.arrayBuffer());
            const data = new Uint8Array(bytes);
            c4w._c4w_req_set_response(req.req_ptr,
                copy_to_c(data, c4w), data.length, node_index);
            if (conf.debug) log(`::: ${path} (len=${data.length} bytes) FETCHED`);

            if (conf.cache && cacheable) conf.cache.set(req, data);
            return;
        } catch (e) {
            last_error = (e instanceof Error) ? e.message : String(e);
        }
        node_index++;
    }
    c4w._c4w_req_set_error(req.req_ptr, as_char_ptr(last_error, c4w, free_buffers), 0);
    free_buffers.forEach(ptr => c4w._free(ptr));
    if (conf.debug) log(`::: ${path} (Error: ${last_error})`);
}


export default class C4Client {


    config: Config;

    constructor(config?: Partial<Config>) {
        this.config = {
            ...{
                chainId: 1, // Default chainId
                beacon_apis: ["https://lodestar-mainnet.chainsafe.io"], // Default beacon API
                rpcs: ["https://rpc.ankr.com/eth"], // Default RPC
                trusted_block_hashes: []
            }, ...config
        }
    }

    private get flags(): number {
        return this.config.include_code ? 1 : 0;
    }


    async createProof(method: string, args: any[]): Promise<Uint8Array> {
        const c4w = await getC4w();
        const free_buffers: number[] = [];
        let ctx = 0;

        try {
            // Call the C function
            ctx = c4w._c4w_create_proof_ctx(
                as_char_ptr(method, c4w, free_buffers),
                as_char_ptr(JSON.stringify(args), c4w, free_buffers),
                BigInt(this.config.chainId),
                this.flags
            );

            while (true) {
                const state = as_json(c4w._c4w_execute_proof_ctx(ctx), c4w, true);

                switch (state.status) {
                    case "success":
                        return as_bytes(state.result, state.result_len, c4w);
                    case "error":
                        throw new Error(state.error);
                    case "waiting": {
                        await Promise.all(state.requests.map((req: DataRequest) => handle_request(req, this.config)));
                        break;
                    }
                }
            }
        } finally {
            free_buffers.forEach(ptr => c4w._free(ptr));
            if (ctx) c4w._c4w_free_proof_ctx(ctx);
        }
    }

    async verifyProof(method: string, args: any[], proof: Uint8Array): Promise<any> {
        const c4w = await getC4w();
        const free_buffers: number[] = [];
        let ctx = 0;

        try {
            // Call the C function
            ctx = c4w._c4w_create_verify_ctx(
                copy_to_c(proof, c4w, free_buffers),
                proof.length,
                as_char_ptr(method, c4w, free_buffers),
                as_char_ptr(JSON.stringify(args), c4w, free_buffers),
                BigInt(this.config.chainId)
            );

            while (true) {
                const state = as_json(c4w._c4w_verify_proof(ctx), c4w, true);

                switch (state.status) {
                    case "success":
                        return state.result;
                    case "error":
                        throw new Error(state.error);
                    case "waiting": {
                        await Promise.all(state.requests.map((req: DataRequest) => handle_request(req, this.config)));
                        break;
                    }
                }
            }
        } finally {
            free_buffers.forEach(ptr => c4w._free(ptr));
            if (ctx) c4w._c4w_free_verify_ctx(ctx);
        }
    }

    async rpc(method: string, args: any[]): Promise<any> {
        // skip verify
        if (this.config.verify && !this.config.verify(method, args))
            return await fetch_rpc(this.config.rpcs, method, args, false);

        let proof = this.config.proofer && this.config.proofer.length
            ? await fetch_rpc(this.config.proofer, method, args, true)
            : await this.createProof(method, args);

        return this.verifyProof(method, args, proof);
    }

    static async register_storage(storage: C4Storage) {
        const c4w = await getC4w();
        c4w.storage = storage;
    }
}


