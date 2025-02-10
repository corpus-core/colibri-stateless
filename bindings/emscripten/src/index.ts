// Import the Emscripten-generated module
import { getC4w, as_char_ptr, as_json, as_bytes, copy_to_c, Storage as C4Storage } from "./wasm.js";

export interface Config {
    chainId: number;
    beacon_apis: string[],
    rpcs: string[];
}

interface DataRequest {
    method: string;
    chain_id: number;
    encoding: string;
    type: string;
    url: string;
    payload: string;
    req_ptr: number;
}

async function handle_request(req: DataRequest, conf: Config) {
    const servers = req.type == "beacon_api" ? conf.beacon_apis : conf.rpcs;
    const c4w = await getC4w();
    for (const server of servers) {
        try {
            const response = await fetch(server + (req.url ? ('/' + req.url) : ''), {
                method: req.method,
                body: req.payload ? JSON.stringify(req.payload) : undefined,
                headers: {
                    "Content-Type": "application/json",
                    "Accept": req.encoding == "json" ? "application/json" : "application/octet-stream"
                }
            }).then(res => res.blob())
                .then(blob => blob.bytes());

            const data_ptr = c4w._malloc(response.length + 1);
            c4w.HEAPU8.set(response, data_ptr);
            c4w.HEAPU8[data_ptr + response.length] = 0;
            c4w._c4w_req_set_response(req.req_ptr, data_ptr, response.length);
            return;
        } catch (e) {
            const errorMessage = (e instanceof Error) ? e.message : String(e);
            c4w._c4w_req_set_error(req.req_ptr, errorMessage);
            return;
        }
    }
}


export default class C4Client {

    config: Config;

    constructor(config?: Partial<Config>) {
        this.config = {
            ...{
                chainId: 1, // Default chainId
                beacon_apis: ["https://lodestar-mainnet.chainsafe.io"], // Default beacon API
                rpcs: ["https://rpc.ankr.com/eth"] // Default RPC
            }, ...config
        }
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
                BigInt(this.config.chainId)
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
        for (let i = 0; i < 5; i++) {
            const free_buffers: number[] = [];
            const result = as_json(c4w._c4w_verify_proof(
                copy_to_c(proof, c4w, free_buffers),
                proof.length,
                as_char_ptr(method, c4w, free_buffers),
                as_char_ptr(JSON.stringify(args), c4w, free_buffers),
                BigInt(this.config.chainId)
            ), c4w, true);
            free_buffers.forEach(ptr => c4w._free(ptr));
            if (result.result !== undefined) return result.result;
            if (result.client_updates) {
                for (const update of result.client_updates) {
                    await handle_request(update, this.config);
                    c4w._c4w_handle_client_updates(update.req_ptr, BigInt(this.config.chainId));
                    c4w._c4w_req_free(update.req_ptr);
                }
                continue;
            }
            throw new Error(result.error);
        }
        throw new Error('too many updates');
    }

    async rpc(method: string, args: any[]): Promise<any> {
        const proof = await this.createProof(method, args);
        return this.verifyProof(method, args, proof);
    }

    static async register_storage(storage: C4Storage) {
        const c4w = await getC4w();
        c4w.storage = storage;
    }
}


