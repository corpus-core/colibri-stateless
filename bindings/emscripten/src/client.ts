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
import { fetch_rpc, handle_request_runtime } from './http.js';
import type { C4Runtime } from './runtime.js';

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

function cleanup_args(method: string, args: any[]): any[] {
    if (method == 'eth_verifyLogs') return args.map(arg => ({
        transactionIndex: arg.transactionIndex,
        blockNumber: arg.blockNumber
    }));
    return args;
}

export default class C4Client {
    config: C4Config;
    private runtime: C4Runtime;
    private eventEmitter: EventEmitter;
    private connectionState: ConnectionState;
    private subscriptionManager: SubscriptionManager;
    private initMap: Map<number | string, boolean> = new Map();
    private flags: number = 0;

    // Protect against prototype pollution by freezing critical methods
    private static readonly CRITICAL_METHODS = ['rpc', 'request', 'verifyProof', 'createProof'] as const;

    static {
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
     * @param runtime Runtime binding for WASM or native backend.
     * @param config Optional partial configuration; user config overrides defaults.
     */
    constructor(runtime: C4Runtime, config?: Partial<C4Config>) {
        this.runtime = runtime;
        const chainId = config?.chainId ? get_chain_id(config?.chainId + '') : 1;
        const chain_config = { ...default_config[chainId + ''] };

        const baseConfig = {
            chains: {},
            trusted_checkpoint: undefined,
            rpcs: chain_config.rpcs || [],
            beacon_apis: chain_config.beacon_apis || [],
            prover: chain_config.prover || [],
            checkpointz: chain_config.checkpointz || [],
            ...config,
            chainId,
        } as C4Config;

        PrototypeProtection.protectConfig(baseConfig, ['rpcs', 'beacon_apis', 'prover', 'checkpointz']);
        this.config = baseConfig;

        if (!this.config.warningHandler)
            this.config.warningHandler = async (req: RequestArguments, message: string) => console.warn(message);
        if (!this.config.proofStrategy)
            this.config.proofStrategy = Strategy.VerifyIfPossible;

        this.eventEmitter = new EventEmitter();
        this.connectionState = new ConnectionState(
            { chainId: parseInt(this.config.chainId + ''), debug: this.config.debug },
            async () => this.rpc('eth_chainId', [], C4MethodType.LOCAL),
            this.eventEmitter
        );

        this.subscriptionManager = new SubscriptionManager(
            async (method: string, params: any[]) => this.rpc(method, params),
            this.eventEmitter,
            {
                debug: this.config.debug,
                pollingInterval: this.config.pollingInterval || chain_conf(this.config, this.config.chainId)?.pollingInterval || 12000
            }
        );
    }

    private async fetch_checkpointz() {
        let checkpoint: string | undefined = undefined;
        for (const url of [...(this.config.checkpointz || []), ...(this.config.beacon_apis || []), ...(this.config.prover || [])]) {
            const response = await fetch(url + (url.endsWith('/') ? '' : '/') + 'eth/v1/beacon/states/head/finality_checkpoints', {
                method: 'GET',
                headers: {
                    'Content-Type': 'application/json'
                }
            });
            if (response.ok) {
                const res = await response.json();
                checkpoint = res?.data?.finalized?.root;
                if (checkpoint) break;
            }
        }
        if (!checkpoint) throw new Error('No checkpoint found');
        this.config.trusted_checkpoint = checkpoint;

        await this.runtime.setTrustedCheckpoint(this.config.chainId as number, checkpoint);
    }

    async getMethodSupport(method: string): Promise<C4MethodType> {
        const res = await this.runtime.getMethodSupport(BigInt(this.config.chainId), method);
        return res as C4MethodType;
    }

    async createProof(method: string, args: any[]): Promise<Uint8Array> {
        const paramsJson = JSON.stringify(args);
        const ctx = await this.runtime.createProverCtx(method, paramsJson, BigInt(this.config.chainId), this.flags);
        try {
            while (true) {
                const state = await this.runtime.proverStep(ctx);
                switch (state.status) {
                    case 'success':
                        return state.proof;
                    case 'error':
                        throw new Error(state.error);
                    case 'waiting':
                        await Promise.all(state.requests.map((req: DataRequest) => handle_request_runtime(req, this.config, this.runtime)));
                        break;
                }
            }
        } finally {
            await this.runtime.freeProverCtx(ctx);
        }
    }

    async verifyProof(method: string, args: any[], proof: Uint8Array): Promise<any> {
        const ctx = await this.runtime.createVerifyCtx(
            proof,
            method,
            JSON.stringify(args),
            BigInt(this.config.chainId),
            this.config.trusted_checkpoint,
            this.config.checkpoint_witness_keys
        );
        try {
            while (true) {
                const state = await this.runtime.verifyStep(ctx);
                switch (state.status) {
                    case 'success':
                        return state.result;
                    case 'error':
                        throw new Error(state.error);
                    case 'waiting':
                        await Promise.all(state.requests.map((req: DataRequest) => handle_request_runtime(req, this.config, this.runtime)));
                        break;
                }
            }
        } finally {
            await this.runtime.freeVerifyCtx(ctx);
        }
    }

    async rpc(method: string, args: any[], method_type?: C4MethodType): Promise<any> {
        if (!this.initMap.get(this.config.chainId)) {
            this.initMap.set(this.config.chainId, true);
            const hasCheckpointz = this.config.checkpointz && this.config.checkpointz.length > 0;
            if (!this.config.zk_proof && hasCheckpointz && !this.config.trusted_checkpoint && (await this.runtime.getProverConfigHex(this.config.chainId as number)).length == 2)
                await this.fetch_checkpointz();
        }

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

                const proof = this.config.prover && this.config.prover.length
                    ? await fetch_rpc(this.config.prover, {
                        method,
                        params: cleanup_args(method, args),
                        c4: await this.runtime.getProverConfigHex(this.config.chainId as number),
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
        throw new ProviderRpcError(-32603, `Internal error: Unhandled method type for ${method} in C4Client.rpc core`);
    }

    static async register_storage(runtime: C4Runtime, storage: any) {
        if (runtime.registerStorage) {
            await runtime.registerStorage(storage);
        }
    }

    async request(args: RequestArguments): Promise<unknown> {
        if (!this.connectionState.initialConnectionAttempted) {
            await this.connectionState.attemptInitialConnection();
        }

        const { method, params } = args;
        const paramsArray = Array.isArray(params) ? params : (params ? [params] : []);

        const subscriptionResult = this.subscriptionManager.handleRequest(method, paramsArray);
        if (subscriptionResult !== null) {
            return subscriptionResult;
        }

        let conf = chain_conf(this.config, this.config.chainId);
        let strategy = conf?.proofStrategy || this.config.proofStrategy;

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
                if (this.config.debug) console.error('[C4Client] Error during lazy initial connection attempt triggered by \'on\':', err);
            });
        }
        return this;
    }

    public removeListener(event: string, callback: (data: any) => void): this {
        this.eventEmitter.removeListener(event, callback);
        return this;
    }
}


