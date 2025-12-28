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
  get_prover_config_hex,
  set_trusted_checkpoint,
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
import { default_config, get_chain_id, chain_conf } from './chains.js';
import { SubscriptionManager, EthSubscribeSubscriptionType, EthNewFilterType } from './subscriptionManager.js';
import Strategy from './strategy.js';
import { TransactionVerifier, PrototypeProtection } from './transactionVerifier.js';
import { fetch_rpc, handle_request } from './http.js';

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

// fetch_rpc und handle_request sind nach http.ts verschoben

// default_config, get_chain_id, chain_conf ausgelagert nach ./chains.ts

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
  private initMap: Map<number | string, boolean> = new Map();
  private flags: number = 0;

  // Protect against prototype pollution by freezing critical methods
  private static readonly CRITICAL_METHODS = ['rpc', 'request', 'verifyProof', 'createProof'] as const;

  static {
    // Apply prototype pollution protection
    PrototypeProtection.protectClass(C4Client, C4Client.CRITICAL_METHODS);
  }




  /**
   * Creates a new Colibri client instance.
   *
   * Example:
   * ```ts
   * import Colibri from '@corpus-core/colibri-stateless';
   * const client = new Colibri({ chainId: 1 });
   * const blockNumber = await client.request({ method: 'eth_blockNumber' });
   * ```
   *
   * @param config Optional partial configuration; user config overrides defaults.
   */
  constructor(config?: Partial<C4Config>) {
    const chainId = config?.chainId ? get_chain_id(config?.chainId + '') : 1;
    const chain_config = { ...default_config[chainId + ''] };


    // Protect against config manipulation (deep-freeze at sensitive boundaries)
    const baseConfig = {
      chains: {},
      trusted_checkpoint: undefined,
      rpcs: chain_config.rpcs || [],
      beacon_apis: chain_config.beacon_apis || [],
      prover: chain_config.prover || [],
      checkpointz: chain_config.checkpointz || [],
      ...config, // User config overrides defaults
      chainId,
    } as C4Config;

    // Apply config immutability/protection
    PrototypeProtection.protectConfig(baseConfig, ['rpcs', 'beacon_apis', 'prover', 'checkpointz']);

    this.config = baseConfig;

    if (!this.config.warningHandler)
      this.config.warningHandler = async (req: RequestArguments, message: string) => console.warn(message)
    if (!this.config.proofStrategy)
      this.config.proofStrategy = Strategy.VerifyIfPossible;

    this.eventEmitter = new EventEmitter();
    this.connectionState = new ConnectionState(
      { chainId: parseInt(this.config.chainId + ''), debug: this.config.debug },
      async () => this.rpc('eth_chainId', [], C4MethodType.LOCAL), // specific callback to detect chainId
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

  // Prover config helpers are moved to wasm.ts (get_prover_config_hex)

  private async fetch_checkpointz() {
    let checkpoint: string | undefined = undefined
    for (const url of [...(this.config.checkpointz || []), ...(this.config.beacon_apis || []), ...(this.config.prover || [])]) {
      const response = await fetch(url + (url.endsWith('/') ? '' : '/') + 'eth/v1/beacon/states/head/finality_checkpoints', {
        method: 'GET',
        headers: {
          "Content-Type": "application/json"
        }
      })
      if (response.ok) {
        const res = await response.json();
        checkpoint = res?.data?.finalized?.root
        if (checkpoint) break;
      }
    }
    if (!checkpoint) throw new Error('No checkpoint found');
    this.config.trusted_checkpoint = checkpoint;

    // set trusted checkpoint in C state so we can use it in proof calls immediately
    await set_trusted_checkpoint(this.config.chainId as number, checkpoint);
  }

  /**
   * Checks whether the RPC method is supported or proofable.
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
   * Creates a proof for the given method and arguments.
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
   * Verifies a proof for the given method and arguments.
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

      // Call the C function
      const checkpoint_ptr = this.config.trusted_checkpoint
        ? as_char_ptr(this.config.trusted_checkpoint, c4w, free_buffers)
        : 0;

      const witness_keys_ptr = this.config.checkpoint_witness_keys
        ? as_char_ptr(this.config.checkpoint_witness_keys, c4w, free_buffers)
        : 0;

      ctx = c4w._c4w_create_verify_ctx(
        copy_to_c(proof, c4w, free_buffers),
        proof.length,
        as_char_ptr(method, c4w, free_buffers),
        as_char_ptr(JSON.stringify(args), c4w, free_buffers),
        BigInt(this.config.chainId),
        checkpoint_ptr,
        witness_keys_ptr);

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
   * Executes an RPC method. This includes:
   * - Creating or fetching the proof
   * - Verifying the proof
   * - Returning the result
   * @param method - The method to execute
   * @param args - The arguments to execute the method with
   * @returns The result
   */
  async rpc(method: string, args: any[], method_type?: C4MethodType): Promise<any> {
    // eth_subscribe and eth_unsubscribe are handled by C4Client.request before this method is called.
    // This rpc method is for the underlying data fetching/proving.
    if (!this.initMap.get(this.config.chainId)) {
      this.initMap.set(this.config.chainId, true);
      if (!this.config.zk_proof && this.config.checkpointz && this.config.checkpointz.length > 0 && !this.config.trusted_checkpoint && (await get_prover_config_hex(this.config.chainId as number)).length == 2)
        await this.fetch_checkpointz();

    }
    // Special handling for eth_sendTransaction with verification
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
          // skip verification and fetch directly
          return await fetch_rpc(this.config.rpcs, { method, params: args }, false);
        }

        const proof = this.config.prover && this.config.prover.length
          ? await fetch_rpc(this.config.prover, {
            method,
            params: cleanup_args(method, args),
            c4: await get_prover_config_hex(this.config.chainId as number),
            zk_proof: !!this.config.zk_proof,
            signers: this.config.checkpoint_witness_keys || '0x'
          }, true)
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

  /**
   * Registers an event listener on the Colibri client.
   * @param event Event name (e.g. 'connect', 'disconnect', 'message')
   * @param callback Callback invoked with event data
   * @return This client for chaining
   */
  public on(event: string, callback: (data: any) => void): this {
    this.eventEmitter.on(event, callback);
    if ((event === 'connect' || event === 'disconnect') && !this.connectionState.initialConnectionAttempted) {
      this.connectionState.attemptInitialConnection().catch(err => {
        if (this.config.debug) console.error("[C4Client] Error during lazy initial connection attempt triggered by 'on':", err);
      });
    }
    return this;
  }

  /**
   * Removes a previously registered event listener from the Colibri client.
   * @param event Event name
   * @param callback Same function reference passed to on()
   * @return This client for chaining
   */
  public removeListener(event: string, callback: (data: any) => void): this {
    this.eventEmitter.removeListener(event, callback);
    return this;
  }
}
