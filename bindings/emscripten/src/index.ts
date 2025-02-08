// Import the Emscripten-generated module
import { loadC4WModule, C4W } from "./c4_module.js";


let module: C4W | null = null;
let modulePromise: Promise<C4W> | null = null;
async function getC4w(): Promise<C4W> {
    if (module) return module;
    if (!modulePromise) {
        modulePromise = loadC4WModule().then((loadedModule) => {
            module = loadedModule;
            modulePromise = null; // Reset the promise after loading
            return module;
        });
    }
    return modulePromise;
}

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

export class C4Proofer {

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

        // Convert method and args to strings
        const argsStr = JSON.stringify(args);

        // Allocate memory for the strings
        const methodPtr = c4w._malloc(method.length + 1);
        const argsPtr = c4w._malloc(argsStr.length + 1);
        let ctx = 0;

        try {
            // Copy the strings into the module's memory
            c4w.stringToUTF8(method, methodPtr, method.length + 1);
            c4w.stringToUTF8(argsStr, argsPtr, argsStr.length + 1);

            // Convert chainId to BigInt
            const chainIdBigInt = BigInt(this.config.chainId);

            // Call the C function
            ctx = c4w._c4w_create_proof_ctx(methodPtr, argsPtr, chainIdBigInt);

            while (true) {
                const resultPtr = c4w._c4w_execute_proof_ctx(ctx);

                // Convert the char* to a JavaScript string
                const resultStr = c4w.UTF8ToString(resultPtr);
                c4w._free(resultPtr);
                const state = JSON.parse(resultStr);
                switch (state.status) {
                    case "success": {
                        const data_ptr = state.result;
                        const data_len = state.result_len;
                        const data = new Uint8Array(data_len);
                        data.set(c4w.HEAPU8.subarray(data_ptr, data_ptr + data_len));
                        return data;
                    }
                    case "waiting": {
                        await Promise.all(state.requests.map((req: DataRequest) => handle_request(req, this.config)));
                        break;
                    }
                    case "error": {
                        throw new Error(state.error);
                    }
                }
            }
        } finally {
            // Free the allocated memory
            c4w._free(methodPtr);
            c4w._free(argsPtr);
            if (ctx) c4w._c4w_free_proof_ctx(ctx);
        }
    }
}
