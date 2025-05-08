// Import the Emscripten-generated module
import {
  as_bytes,
  as_char_ptr,
  as_json,
  C4W,
  copy_to_c,
  getC4w,
  Storage as C4Storage
} from "./wasm.js";
import { EventEmitter } from './eventEmitter.js';
import { ConnectionState } from './connectionState.js';
import {
  ProviderRpcError,
  RequestArguments,
  Cache,
  Config as C4Config,
  DataRequest,
  MethodType,
  ProviderMessage
} from './types.js';
import { SubscriptionManager, RpcCaller, EthSubscribeSubscriptionType, EthNewFilterType } from './subscriptionManager';

// Helper function for chain ID formatting (can be used by ConnectionManager and C4Client)
function formatChainId(value: any, debug?: boolean): string | null {
  if (typeof value === 'string') {
    if (value.startsWith('0x')) {
      return value.toLowerCase();
    }
    const parsed = parseInt(value, 10);
    if (!isNaN(parsed)) {
      return '0x' + parsed.toString(16);
    }
  } else if (typeof value === 'number') {
    return '0x' + value.toString(16);
  }
  if (debug) {
    console.warn('Could not format chainId:', value);
  }
  return null;
}

async function fetch_rpc(urls: string[], payload: any, as_proof: boolean = false) {
  let last_error = "All nodes failed";
  for (const url of urls) {
    const response = await fetch(url, {
      method: 'POST',
      body: JSON.stringify({ id: 1, jsonrpc: "2.0", ...payload }),
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
    else
      last_error = `HTTP error! Status: ${response.status}, Details: ${await response.text()}`;
  }
  throw new Error(last_error);
}
function log(msg: string) {
  console.error(msg);
}
export async function handle_request(req: DataRequest, conf: C4Config) {

  const free_buffers: number[] = [];
  const servers = req.type == "beacon_api" ? ((conf.proofer && conf.proofer.length) ? conf.proofer : conf.beacon_apis) : conf.rpcs;
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

function check_trusted_blockhashes(trusted_block_hashes: string[], c4w: C4W, chainId: number) {
  const blockhashes = new Uint8Array(32 * trusted_block_hashes.length);
  for (let i = 0; i < trusted_block_hashes.length; i++) {
    if (trusted_block_hashes[i].length != 66 || !trusted_block_hashes[i].startsWith("0x")) throw new Error("Invalid trustedblockhash : " + trusted_block_hashes[i]);
    for (let j = 0; j < 32; j++)
      blockhashes[i * 32 + j] = parseInt(trusted_block_hashes[i].slice(2 + j * 2, 4 + j * 2), 16);
  }
  const ptr = copy_to_c(blockhashes, c4w);
  c4w._c4w_set_trusted_blockhashes(BigInt(chainId), ptr, blockhashes.length);
  c4w._free(ptr);
  trusted_block_hashes.length = 0;
}

export default class C4Client {

  config: C4Config;
  private eventEmitter: EventEmitter;
  private connectionState: ConnectionState;
  private subscriptionManager: SubscriptionManager;

  constructor(config?: Partial<C4Config>) {
    this.config = {
      // Defaults including pollingInterval if not provided
      chainId: 1,
      beacon_apis: ["https://lodestar-mainnet.chainsafe.io"],
      rpcs: ["https://rpc.ankr.com/eth"],
      trusted_block_hashes: [],
      proofer: ["https://mainnet.colibri-proof.tech"],
      pollingInterval: 12000, // Default here, can be overridden by user config
      // ... other defaults like debug: false etc. might be useful
      ...config // User config overrides defaults
    };

    this.eventEmitter = new EventEmitter();

    // Specific callback for ConnectionState for eth_chainId
    const fetchChainIdForConnectionState = async () => this.rpc('eth_chainId', []);

    // General RpcCaller for SubscriptionManager
    const generalRpcCaller: RpcCaller = async (method: string, params: any[]) => {
      return this.rpc(method, params);
    };

    this.connectionState = new ConnectionState(
      { chainId: this.config.chainId, debug: this.config.debug },
      fetchChainIdForConnectionState, // Use the specific callback
      this.eventEmitter,
      formatChainId
    );

    this.subscriptionManager = new SubscriptionManager(
      generalRpcCaller, // Use the general rpc caller
      this.eventEmitter,
      { debug: this.config.debug, pollingInterval: this.config.pollingInterval }
    );
  }

  private async getProoferConfig() {
    const c4w = await getC4w();
    if (!c4w.storage) return '0x'
    const state = c4w.storage.get('state_' + this.config.chainId)
    return '0x' + (state ? Array.from(state).map(_ => _.toString(16).padStart(2, '0')).join('') : '')
  }

  private get flags(): number {
    return this.config.include_code ? 1 : 0;
  }

  /**
   * checks, whether the rpc-method is supported or proofable.
   * @param method - The method to check
   * @returns The method type
   */
  async getMethodSupport(method: string): Promise<MethodType> {
    const c4w = await getC4w();
    const free_buffers: number[] = [];
    const method_type = c4w._c4w_get_method_type(BigInt(this.config.chainId), as_char_ptr(method, c4w, free_buffers));
    free_buffers.forEach(ptr => c4w._free(ptr));
    return method_type;
  }

  /**
   * creates a proof for the given method and arguments
   * @param method - The method to create a proof for
   * @param args - The arguments to create a proof for
   * @returns The proof
   */
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
        this.flags);

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

  /**
   * verifies a proof for the given method and arguments
   * @param method - The method to verify the proof for
   * @param args - The arguments to verify the proof for
   * @param proof - The proof to verify
   * @returns The result
   */
  async verifyProof(method: string, args: any[], proof: Uint8Array): Promise<any> {
    const c4w = await getC4w();
    const free_buffers: number[] = [];
    let ctx = 0;

    try {

      if (this.config.trusted_block_hashes && this.config.trusted_block_hashes.length > 0)
        check_trusted_blockhashes(this.config.trusted_block_hashes, c4w, this.config.chainId);

      // Call the C function
      ctx = c4w._c4w_create_verify_ctx(
        copy_to_c(proof, c4w, free_buffers),
        proof.length,
        as_char_ptr(method, c4w, free_buffers),
        as_char_ptr(JSON.stringify(args), c4w, free_buffers),
        BigInt(this.config.chainId));

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

  /**
   * executes a rpc-method, which includes
   * -creating or fetching the proof
   * -verifying the proof
   * -returning the result
   * @param method - The method to execute
   * @param args - The arguments to execute the method with
   * @returns The result
   */
  async rpc(method: string, args: any[]): Promise<any> {
    // eth_subscribe and eth_unsubscribe are handled by C4Client.request before this method is called.
    // This rpc method is for the underlying data fetching/proving.

    const method_type = await this.getMethodSupport(method);

    switch (method_type) {
      case MethodType.PROOFABLE: {
        if (this.config.verify && !this.config.verify(method, args)) {
          return await fetch_rpc(this.config.rpcs, { method, params: args }, false);
        }
        const proof = this.config.proofer && this.config.proofer.length
          ? await fetch_rpc(this.config.proofer, { method, params: args, c4: await this.getProoferConfig() }, true)
          : await this.createProof(method, args);
        return this.verifyProof(method, args, proof);
      }
      case MethodType.UNPROOFABLE:
        return await fetch_rpc(this.config.rpcs, { method, params: args }, false);
      case MethodType.NOT_SUPPORTED:
        throw new ProviderRpcError(4200, `Method ${method} is not supported by C4Client.rpc core`);
      case MethodType.LOCAL:
        if (method === 'eth_chainId') {
          return this.connectionState.currentChainId || formatChainId(this.config.chainId, this.config.debug);
        }
        throw new ProviderRpcError(4200, `Method ${method} is LOCAL but not currently handled by C4Client.rpc core`);
    }
    // Should be unreachable if MethodType enum is comprehensive and handled
    throw new ProviderRpcError(-32603, `Internal error: Unhandled method type for ${method} in C4Client.rpc core`);
  }

  static async register_storage(storage: C4Storage) {
    const c4w = await getC4w();
    c4w.storage = storage;
  }

  async request(args: RequestArguments): Promise<unknown> {
    if (!this.connectionState.initialConnectionAttempted) {
      await this.connectionState.attemptInitialConnection();
    }

    const { method, params } = args;
    const paramsArray = Array.isArray(params) ? params : (params ? [params] : []);

    // Attempt to handle with SubscriptionManager first
    const subscriptionResult = this.subscriptionManager.handleRequest(method, paramsArray);
    if (subscriptionResult !== null) {
      // If handleRequest returns a Promise, it means it's handling the request
      return subscriptionResult;
    }

    // If not handled by SubscriptionManager, proceed with standard RPC call logic
    try {
      const result = await this.rpc(method, paramsArray);
      this.connectionState.processSuccessfulRequest(method, result);
      return result;
    } catch (error: any) {
      const providerError = ProviderRpcError.createError(error);
      this.connectionState.processFailedRequest(providerError);
      throw providerError;
    }
  }

  public on(event: string, callback: (data: any) => void): this {
    this.eventEmitter.on(event, callback);
    if ((event === 'connect' || event === 'disconnect') && !this.connectionState.initialConnectionAttempted) {
      this.connectionState.attemptInitialConnection().catch(err => {
        if (this.config.debug) console.error("[C4Client] Error during lazy initial connection attempt triggered by 'on':", err);
      });
    }
    return this;
  }

  public removeListener(event: string, callback: (data: any) => void): this {
    this.eventEmitter.removeListener(event, callback);
    return this;
  }
}
