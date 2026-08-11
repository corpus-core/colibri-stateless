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

// Runtime abstraction: WASM (browser/default) or native addon (Node.js entry)
import { getRuntime, type Storage as C4Storage } from './runtime.js';
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
  ChainConfig,
  ProverMode
} from './types.js';
import { default_config, get_chain_id, chain_conf } from './chains.js';
import { SubscriptionManager, EthSubscribeSubscriptionType, EthNewFilterType } from './subscriptionManager.js';
import Strategy from './strategy.js';
import { TransactionVerifier, PrototypeProtection } from './transactionVerifier.js';
import { fetch_rpc, handle_request } from './http.js';

export { Strategy };

// Public helper for controlling WASM loading behavior in Node/bundlers.
export { set_wasm_url } from "./wasm.js";

// Runtime introspection (e.g. to check whether the native addon is active in Node).
export { getRuntime, type C4Runtime, type RuntimeStatus } from './runtime.js';

/**
 * Decodes a serialized proof into its JSON representation using the active runtime.
 * @param proof The proof bytes
 * @return The decoded proof as JSON
 */
export async function decode_proof(proof: Uint8Array): Promise<any> {
  return (await getRuntime()).decodeProof(proof);
}

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
  EthNewFilterType,
  ProverMode
};

// Re-export transaction verification utilities
export {
  TransactionVerifier,
  PrototypeProtection
} from './transactionVerifier.js';

// EIP-3668 / CCIP-Read: the EVM ran to completion but reverted. Map the
// verifier's `revert` status to a structured EIP-1193 error (code 3 + data)
// so clients like ethers can decode OffchainLookup payloads from
// `error.data` and fetch the offchain gateway.
function throwRevert(data: unknown): never {
  throw new ProviderRpcError(3, "execution reverted", data);
}

export default class C4Client {

  config: C4Config;
  private eventEmitter: EventEmitter;
  private connectionState: ConnectionState;
  private subscriptionManager: SubscriptionManager;
  private flags: number = 0;
  private verify_flags: number = 0;
  private _lightClientTimer: ReturnType<typeof setInterval> | null = null;

  /** Default freshness window for `latest` proofs in seconds (≈5 Ethereum slots). */
  private static readonly DEFAULT_MAX_LATEST_AGE_SECONDS = 60;

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

    if (this.config.include_code) this.flags |= 1;
    // Default true: eth_createAccessList. Opt out (false) → USE_DEBUG_TRACE (bit 6).
    if (this.config.use_accesslist === false) this.flags |= (1 << 6);
    if (this.config.privacy_mode === 'basic' || this.config.oblivious_nodes?.length) this.verify_flags |= 2;
    if (this.config.oblivious_nodes?.length) this.verify_flags |= (1 << 6);
    if (this.config.skip_wsp_check) this.verify_flags |= (1 << 7);
    if (this.config.logs_completeness) { this.flags |= (1 << 12); this.verify_flags |= (1 << 9); }

    if (!this.config.warningHandler)
      this.config.warningHandler = async (req: RequestArguments, message: string) => console.warn(message)
    if (!this.config.proofStrategy)
      this.config.proofStrategy = Strategy.WarningWithFallback;

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

  /**
   * Computes the lower bound for `block.timestamp` accepted on `"latest"`
   * proofs as `now - max_latest_age_seconds`. Returns `0n` when the host
   * disables the check (`max_latest_age_seconds === 0`). The platform
   * wallclock is read here in the binding so the C/WASM core stays
   * clock-free (relevant for embedded/WASM portability).
   */
  private get_min_latest_block_ts(): bigint {
    const maxAge = this.config.max_latest_age_seconds ?? C4Client.DEFAULT_MAX_LATEST_AGE_SECONDS;
    if (maxAge <= 0) return 0n;
    const nowSec = Math.floor(Date.now() / 1000);
    const lower = nowSec - maxAge;
    return BigInt(lower > 0 ? lower : 0);
  }

  /**
   * Checks whether the RPC method is supported or proofable.
   * @param method - The method to check
   * @param args - Optional method arguments (used in PAP mode to check cached data availability)
   * @returns The method type
   */
  async getMethodSupport(method: string, args?: any[]): Promise<C4MethodType> {
    const runtime = await getRuntime();
    const paramsStr = args ? JSON.stringify(args) : null;
    return runtime.getMethodType(BigInt(this.config.chainId), method, paramsStr, this.verify_flags) as C4MethodType;
  }

  /**
   * Creates a proof for the given method and arguments.
   * @param method - The method to create a proof for
   * @param args - The arguments to create a proof for
   * @returns The proof
   */
  async createProof(method: string, args: any[]): Promise<Uint8Array> {
    const runtime = await getRuntime();
    let ctx = null;

    try {
      ctx = runtime.createProverCtx(method, JSON.stringify(args), BigInt(this.config.chainId), this.flags);

      while (true) {
        const state = runtime.executeProverCtx(ctx);

        switch (state.status) {
          case "success":
            return state.result;
          case "error":
            throw new Error(state.error);
          case "pending": {
            await Promise.all(state.requests!.map((req: DataRequest) => handle_request(runtime, req, this.config)));
            break;
          }
        }
      }
    } finally {
      if (ctx) runtime.freeProverCtx(ctx);
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
    const runtime = await getRuntime();
    let ctx = null;

    try {
      ctx = runtime.createVerifyCtx(
        proof,
        method,
        JSON.stringify(args),
        BigInt(this.config.chainId),
        this.config.trusted_checkpoint || null,
        this.config.checkpoint_witness_keys || null,
        this.verify_flags,
        this.get_min_latest_block_ts());

      while (true) {
        const state = runtime.verifyProof(ctx);

        switch (state.status) {
          case "success":
            return state.result;
          case "revert":
            throwRevert(state.data);
          case "error":
            throw new Error(state.error);
          case "pending": {
            await Promise.all(state.requests!.map((req: DataRequest) => handle_request(runtime, req, this.config)));
            break;
          }
        }
      }
    } finally {
      if (ctx) runtime.freeVerifyCtx(ctx);
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
    // eth_sendTransaction stays in bindings (requires fallback_provider interaction)
    if (method === 'eth_sendTransaction' && (this.config as any).verifyTransactions) {
      return await TransactionVerifier.verifyAndSendTransaction(
        args[0],
        this.config,
        (method, args, methodType) => this.rpc(method, args, methodType),
        fetch_rpc,
        this.config.fetch
      );
    }

    // Allow host to skip verification for specific calls
    if (method_type === undefined)
      method_type = await this.getMethodSupport(method, args);
    if (method_type === C4MethodType.PROOFABLE && this.config.verify && !this.config.verify(method, args))
      return await fetch_rpc(this.config.rpcs, { method, params: args }, false, this.config.fetch);

    const runtime = await getRuntime();
    let ctx = null;

    try {
      const prover_mode_map: Record<ProverMode, number> = { local: 0, remote: 1, hybrid: 2, proxy: 3, light_client: 4 };
      const resolved_mode = this.config.prover_mode ?? (this.config.prover?.length ? 'remote' : 'local');
      const native_mode = resolved_mode === 'light_client' ? prover_mode_map['hybrid'] : prover_mode_map[resolved_mode];
      let prover_flags = this.flags;
      if (this.config.zk_proof) prover_flags |= (1 << 7);

      ctx = runtime.createRpcCtx(
        method,
        JSON.stringify(args || []),
        BigInt(this.config.chainId),
        prover_flags,
        this.verify_flags,
        native_mode
      );

      if (resolved_mode === 'proxy')
        runtime.rpcCtxSetProxyUrls(ctx, this.config.rpcs.join(','), this.config.beacon_apis.join(','));

      if (this.config.trusted_checkpoint)
        runtime.setCheckpoint(BigInt(this.config.chainId), this.config.trusted_checkpoint);
      if (this.config.checkpoint_witness_keys)
        runtime.rpcCtxSetWitnessKeys(ctx, this.config.checkpoint_witness_keys);

      runtime.rpcCtxSetMinLatestBlockTs(ctx, this.get_min_latest_block_ts());

      while (true) {
        const state = runtime.executeRpcCtx(ctx);
        switch (state.status) {
          case "success":
            return state.result;
          case "revert":
            throwRevert(state.data);
          case "error":
            if (state.error!.includes("zk sync proof") && this.config.zk_proof) {
              // try without zk proof
              this.config.zk_proof = false;
              return await this.rpc(method, args, method_type);
            }
            throw new Error(state.error);
          case "pending":
            await Promise.all(state.requests!.map((req: DataRequest) => handle_request(runtime, req, this.config)));
            break;
        }
      }
    } finally {
      if (ctx) runtime.freeRpcCtx(ctx);
    }
  }


  static async register_storage(storage: C4Storage) {
    (await getRuntime()).registerStorage(storage);
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

  /** Stops light client polling and releases resources. Call when the client is no longer needed. */
  destroy(): void {
    this.stopLightClient();
  }

  /**
   * Starts background polling to keep the block header cache warm.
   * Useful for `prover_mode: "light_client"`.
   *
   * By default polls `eth_getBlockHeader("latest")` which fetches only the
   * compact header proof. Set `fullBlock` to `true` to poll
   * `eth_getBlockByNumber("latest")` instead -- useful when many
   * `eth_getTransactionByHash` / `eth_getTransactionReceipt` calls follow,
   * since those need the full block data.
   *
   * @param intervalMs polling interval in milliseconds (default: 12000 = one Ethereum slot)
   * @param fullBlock if true, fetch the full block instead of just the header (default: false)
   */
  startLightClient(intervalMs: number = 12000, fullBlock: boolean = false): void {
    this.stopLightClient();
    const method = fullBlock ? 'eth_getBlockByNumber' : 'eth_getBlockHeader';
    const params = fullBlock ? ['latest', false] : ['latest'];
    this._lightClientTimer = setInterval(async () => {
      try {
        await this.rpc(method, params);
      } catch (e) {
        if (this.config.debug) console.warn('[C4Client] lightClient poll error:', e);
      }
    }, intervalMs);
  }

  /** Stops background light client polling. */
  stopLightClient(): void {
    if (this._lightClientTimer !== null) {
      clearInterval(this._lightClientTimer);
      this._lightClientTimer = null;
    }
  }
}
