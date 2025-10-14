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
  MethodType as C4MethodType,
  ProviderMessage,
  ChainConfig
} from './types.js';
import { SubscriptionManager, EthSubscribeSubscriptionType, EthNewFilterType } from './subscriptionManager.js';
import Strategy from './strategy.js';
import { TransactionVerifier, PrototypeProtection } from './transactionVerifier.js';

export { Strategy };

// Re-export types needed by consumers of the C4Client module
export {
  ProviderRpcError,
  RequestArguments,
  Cache,
  C4Config,
  DataRequest,
  C4MethodType as MethodType,
  ProviderMessage,
  EthSubscribeSubscriptionType,
  EthNewFilterType
};

// Re-export transaction verification utilities
export {
  TransactionVerifier,
  PrototypeProtection
} from './transactionVerifier.js';

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
  const servers = req.type == "checkpointz"
    ? (conf.checkpointz || [])
    : (req.type == "beacon_api"
      ? ((conf.proofer && conf.proofer.length) ? conf.proofer : conf.beacon_apis)
      : conf.rpcs);
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
      c4w._c4w_req_set_response(req.req_ptr, copy_to_c(data, c4w), data.length, node_index);
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

const default_config: {
  [key: string]: {
    alias: string[];
    beacon_apis: string[];
    rpcs: string[];
    proofer: string[];
    checkpointz: string[];
    pollingInterval: number;
  }
} = {
  '1': { // mainnet
    alias: ["mainnet", "eth", "0x1"],
    beacon_apis: ["https://lodestar-mainnet.chainsafe.io"],
    rpcs: ["https://rpc.ankr.com/eth"],
    proofer: ["https://mainnet1.colibri-proof.tech"],
    checkpointz: ["https://sync-mainnet.beaconcha.in", "https://beaconstate.info", "https://sync.invis.tools", "https://beaconstate.ethstaker.cc"],
    pollingInterval: 12000,
  },
  '11155111': { // Sepolia
    alias: ["sepolia", "0xaa36a7"],
    beacon_apis: ["https://ethereum-sepolia-beacon-api.publicnode.com"],
    rpcs: ["https://ethereum-sepolia-rpc.publicnode.com"],
    proofer: ["https://sepolia.colibri-proof.tech"],
    checkpointz: [], // No public checkpointz for Sepolia yet
    pollingInterval: 12000,
  },
  '100': { // gnosis
    alias: ["gnosis", "xdai", "0x64"],
    beacon_apis: ["https://gnosis.colibri-proof.tech"],
    rpcs: ["https://rpc.ankr.com/gnosis"],
    proofer: ["https://gnosis.colibri-proof.tech"],
    checkpointz: [], // TODO: Add Gnosis checkpointz servers
    pollingInterval: 5000,
  },
  '10200': { // gnosis chiado
    alias: ["chiado", "0x27d8"],
    beacon_apis: ["https://gnosis-chiado-beacon-api.publicnode.com"],
    rpcs: ["https://gnosis-chiado-rpc.publicnode.com"],
    proofer: ["https://chiado.colibri-proof.tech"],
    checkpointz: [], // No public checkpointz for Chiado yet
    pollingInterval: 5000,
  },
}

function get_chain_id(chain_id: string): number {
  const chain_id_num = parseInt(chain_id);
  if (!isNaN(chain_id_num)) return chain_id_num;
  for (const chain in default_config) {
    if (default_config[chain].alias.includes(chain_id))
      return parseInt(chain);
  }
  throw new Error("Invalid chain id: " + chain_id);
}

function chain_conf(config: C4Config, chainId: number | string): ChainConfig | undefined {
  const k = config?.chains?.[chainId as number];
  if (k) return k;
  const k2 = default_config[chainId + ''];
  if (k2) return k2 as any;
  return undefined;
}

function cleanup_args(method: string, args: any[]): any[] {
  if (method == "eth_verifyLogs") return args.map(arg => ({
    transactionIndex: arg.transactionIndex,
    blockNumber: arg.blockNumber
  }))
  return args;
}



export default class C4Client {

  config: C4Config;
  private eventEmitter: EventEmitter;
  private connectionState: ConnectionState;
  private subscriptionManager: SubscriptionManager;

  // Schutz vor Prototype Pollution durch Einfrieren kritischer Methoden
  private static readonly CRITICAL_METHODS = ['rpc', 'request', 'verifyProof', 'createProof'] as const;

  static {
    // Schutz vor Prototype Pollution anwenden
    PrototypeProtection.protectClass(C4Client, C4Client.CRITICAL_METHODS);
  }




  constructor(config?: Partial<C4Config>) {
    const chainId = config?.chainId ? get_chain_id(config?.chainId + '') : 1;
    const chain_config = { ...default_config[chainId + ''] };


    // Schutz vor Config-Manipulation durch Deep-Freeze
    const baseConfig = {
      chains: {},
      trusted_block_hashes: [],
      rpcs: chain_config.rpcs || [],
      beacon_apis: chain_config.beacon_apis || [],
      proofer: chain_config.proofer || [],
      checkpointz: chain_config.checkpointz || [],
      ...config, // User config overrides defaults
      chainId,
    } as C4Config;

    // Schutz vor Config-Manipulation anwenden
    PrototypeProtection.protectConfig(baseConfig, ['rpcs', 'beacon_apis', 'proofer', 'checkpointz']);

    this.config = baseConfig;

    if (!this.config.warningHandler)
      this.config.warningHandler = async (req: RequestArguments, message: string) => console.warn(message)
    if (!this.config.proofStrategy)
      this.config.proofStrategy = Strategy.VerifyIfPossible;

    this.eventEmitter = new EventEmitter();
    this.connectionState = new ConnectionState(
      { chainId: parseInt(this.config.chainId + ''), debug: this.config.debug },
      async () => this.rpc('eth_chainId', [], C4MethodType.LOCAL), // Use the specific callback
      this.eventEmitter
    );

    this.subscriptionManager = new SubscriptionManager(
      async (method: string, params: any[]) => this.rpc(method, params),
      this.eventEmitter,
      {
        debug: this.config.debug,
        pollingInterval: this.config.pollingInterval || chain_conf(this.config, this.config.chainId)?.pollingInterval || 12000
      }
    )
  }

  private async getProoferConfig() {
    const c4w = await getC4w();
    if (!c4w.storage) return '0x'
    const state = c4w.storage.get('states_' + this.config.chainId)
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
  async getMethodSupport(method: string): Promise<C4MethodType> {
    const c4w = await getC4w();
    const free_buffers: number[] = [];
    const method_type = c4w._c4w_get_method_type(BigInt(this.config.chainId), as_char_ptr(method, c4w, free_buffers));
    free_buffers.forEach(ptr => c4w._free(ptr));
    return method_type as C4MethodType;
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
        check_trusted_blockhashes(this.config.trusted_block_hashes, c4w, parseInt(this.config.chainId as any));

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
  async rpc(method: string, args: any[], method_type?: C4MethodType): Promise<any> {
    // eth_subscribe and eth_unsubscribe are handled by C4Client.request before this method is called.
    // This rpc method is for the underlying data fetching/proving.

    // Spezielle Behandlung für eth_sendTransaction mit Verifikation
    if (method === 'eth_sendTransaction' && (this.config as any).verifyTransactions) {
      return await TransactionVerifier.verifyAndSendTransaction(
        args[0],
        this.config,
        (method, args, methodType) => this.rpc(method, args, methodType),
        fetch_rpc
      );
    }

    if (method_type === undefined)
      method_type = await this.getMethodSupport(method);

    switch (method_type) {
      case C4MethodType.PROOFABLE: {
        if (this.config.verify && !this.config.verify(method, args)) {
          return await fetch_rpc(this.config.rpcs, { method, params: args }, false);
        }
        const proof = this.config.proofer && this.config.proofer.length
          ? await fetch_rpc(this.config.proofer, { method, params: cleanup_args(method, args), c4: await this.getProoferConfig() }, true)
          : await this.createProof(method, args);
        return this.verifyProof(method, args, proof);
      }
      case C4MethodType.UNPROOFABLE:
        return await fetch_rpc(this.config.rpcs, { method, params: args }, false);
      case C4MethodType.NOT_SUPPORTED:
        throw new ProviderRpcError(4200, `Method ${method} is not supported by C4Client.rpc core`);
      case C4MethodType.LOCAL:
        return this.verifyProof(method, args, new Uint8Array());
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

    let conf = chain_conf(this.config, this.config.chainId)
    let strategy = conf?.proofStrategy || this.config.proofStrategy;

    // If not handled by SubscriptionManager, proceed with standard RPC call logic
    try {
      const result = strategy
        ? await strategy(this, args, this.config, fetch_rpc)
        : await this.rpc(method, paramsArray);
      this.connectionState.processSuccessfulRequest(method, result);
      return result;
    } catch (error: any) {
      const providerError = ProviderRpcError.createError(error, args);
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
