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
import { ProviderRpcError, ProviderMessage } from './types.js'; // Removed unused Config, DataRequest, RequestArguments

const DEFAULT_POLLING_INTERVAL = 12000; // 12 seconds

// Types for subscriptions and filters
export type EthSubscribeSubscriptionType = 'newHeads' | 'logs';
export type EthNewFilterType = 'block' | 'pendingTransaction' | 'log';

export interface Subscription {
    id: string;
    subscriptionMethod: 'eth_subscribe' | 'eth_newFilter';
    kind: EthSubscribeSubscriptionType | EthNewFilterType;
    params?: any; // Filter criteria for 'logs' (subscribe) or 'log' (filter)
    lastPolledBlockNumber?: bigint;
    // Only for eth_subscribe subscriptions with active polling
    timeoutId?: number;
    // internal marker, could also be derived from subscriptionMethod
    // isActivePolling: boolean; // True if managed by setInterval/setTimeout polling loop
}

interface SubscriptionManagerConfig {
    debug?: boolean;
    pollingInterval?: number;
}

// Define a type for the RPC calling function that SubscriptionManager will use
export type RpcCaller = (method: string, params: any[]) => Promise<any>;

export class SubscriptionManager {
    private activeSubscriptions: Map<string, Subscription> = new Map();
    private eventEmitter: EventEmitter;
    private rpcCall: RpcCaller;
    private config: SubscriptionManagerConfig;
    private nextSubscriptionId: number = 0;

    constructor(
        rpcCallCallback: RpcCaller,
        eventEmitter: EventEmitter,
        config: SubscriptionManagerConfig = {}
    ) {
        this.rpcCall = rpcCallCallback;
        this.eventEmitter = eventEmitter;
        this.config = {
            pollingInterval: DEFAULT_POLLING_INTERVAL,
            ...config,
        };
    }

    // New method to handle requests related to subscriptions and filters
    public handleRequest(method: string, paramsArray: any[]): Promise<any> | null {
        switch (method) {
            case 'eth_subscribe':
                if (paramsArray.length < 1 || typeof paramsArray[0] !== 'string') {
                    return Promise.reject(new ProviderRpcError(-32602, 'Invalid params for eth_subscribe: missing subscription type.'));
                }
                return this.subscribe(paramsArray[0] as EthSubscribeSubscriptionType, paramsArray.slice(1));

            case 'eth_unsubscribe':
                if (paramsArray.length < 1 || typeof paramsArray[0] !== 'string') {
                    return Promise.reject(new ProviderRpcError(-32602, 'Invalid params for eth_unsubscribe: missing or invalid subscription ID.'));
                }
                return this.unsubscribe(paramsArray[0] as string);

            case 'eth_newBlockFilter':
                return this.newFilter('block');

            case 'eth_newPendingTransactionFilter':
                // This will internally throw, but we return the promise from newFilter
                return this.newFilter('pendingTransaction');

            case 'eth_newFilter': // For log filters
                return this.newFilter('log', paramsArray);

            case 'eth_getFilterChanges':
                if (paramsArray.length < 1 || typeof paramsArray[0] !== 'string') {
                    return Promise.reject(new ProviderRpcError(-32602, 'Invalid params for eth_getFilterChanges: missing or invalid filter ID.'));
                }
                return this.getFilterChanges(paramsArray[0] as string);

            case 'eth_uninstallFilter':
                if (paramsArray.length < 1 || typeof paramsArray[0] !== 'string') {
                    return Promise.reject(new ProviderRpcError(-32602, 'Invalid params for eth_uninstallFilter: missing or invalid filter ID.'));
                }
                return this.uninstallFilter(paramsArray[0] as string);

            default:
                // Not a method handled by SubscriptionManager
                return null;
        }
    }

    private generateSubscriptionId(): string {
        this.nextSubscriptionId++;
        // Simple hex string, similar to what nodes return
        return `0x${this.nextSubscriptionId.toString(16)}`;
    }

    // For eth_subscribe
    public async subscribe(kind: EthSubscribeSubscriptionType, params: any[] = []): Promise<string> {
        if (this.config.debug) {
            console.log(`[SubMan] eth_subscribe: kind=${kind}, params=`, params);
        }

        const subId = this.generateSubscriptionId();
        let subscriptionDetails: Partial<Subscription> = {
            id: subId,
            subscriptionMethod: 'eth_subscribe',
            kind: kind,
        };

        switch (kind) {
            case 'newHeads':
                // No specific params needed from client for newHeads
                break;
            case 'logs':
                subscriptionDetails.params = params[0] || {};
                break;
            default:
                // Should not happen if kind is correctly typed
                throw new ProviderRpcError(4200, `Subscription kind "${kind}" is not supported by eth_subscribe.`);
        }

        const finalSubscription = subscriptionDetails as Subscription;
        this.activeSubscriptions.set(subId, finalSubscription);
        this.schedulePoll(finalSubscription, 0); // Start active polling

        if (this.config.debug) {
            console.log(`[SubMan] Created eth_subscribe subscription: ${subId}`, finalSubscription);
        }
        return subId;
    }

    // For eth_newFilter, eth_newBlockFilter, eth_newPendingTransactionFilter
    public async newFilter(kind: EthNewFilterType, params: any[] = []): Promise<string> {
        if (this.config.debug) {
            console.log(`[SubMan] eth_newFilter: kind=${kind}, params=`, params);
        }

        if (kind === 'pendingTransaction') {
            if (this.config.debug) console.warn('[SubMan] eth_newPendingTransactionFilter is not supported.');
            throw new ProviderRpcError(4200, 'eth_newPendingTransactionFilter is not supported.');
        }

        const filterId = this.generateSubscriptionId();
        const filterDetails: Subscription = {
            id: filterId,
            subscriptionMethod: 'eth_newFilter',
            kind: kind,
            params: kind === 'log' ? (params[0] || {}) : undefined,
            // No timeoutId as polling is manual via getFilterChanges
        };

        this.activeSubscriptions.set(filterId, filterDetails);
        if (this.config.debug) {
            console.log(`[SubMan] Created eth_newFilter filter: ${filterId}`, filterDetails);
        }
        return filterId;
    }

    // For eth_getFilterChanges
    public async getFilterChanges(filterId: string): Promise<any[]> {
        const sub = this.activeSubscriptions.get(filterId);
        if (!sub || sub.subscriptionMethod !== 'eth_newFilter') {
            if (this.config.debug) console.warn(`[SubMan] getFilterChanges: Filter not found or not a filter type for ID ${filterId}`);
            throw new ProviderRpcError(-32000, 'Filter not found.'); // Standard Ethereum error for missing filter
        }

        if (this.config.debug) {
            console.log(`[SubMan] getFilterChanges for filter: ${filterId}, kind: ${sub.kind}`);
        }

        // Perform a one-time poll based on the filter kind
        return this.executePollLogic(sub, true); // Pass true to indicate results should be returned, not emitted
    }

    // For eth_uninstallFilter
    public async uninstallFilter(filterId: string): Promise<boolean> {
        const sub = this.activeSubscriptions.get(filterId);
        if (sub && sub.subscriptionMethod === 'eth_newFilter') {
            this.activeSubscriptions.delete(filterId);
            // No timeout to clear for filters
            if (this.config.debug) {
                console.log(`[SubMan] Uninstalled filter: ${filterId}`);
            }
            return true;
        }
        if (this.config.debug) {
            console.warn(`[SubMan] UninstallFilter failed: Filter not found or not a filter type for ID ${filterId}`);
        }
        return false;
    }

    // Common polling logic, can return results or emit them via eventEmitter
    private async executePollLogic(sub: Subscription, returnResults: boolean = false): Promise<any[]> {
        let results: any[] = [];
        let currentVerifiableBlockNumber: bigint | null = null;

        try {
            const blockNumberHex = await this.rpcCall('eth_blockNumber', []);
            if (blockNumberHex) currentVerifiableBlockNumber = BigInt(blockNumberHex);
        } catch (e) {
            if (this.config.debug) console.warn('[SubMan] Could not fetch eth_blockNumber for polling context:', e);
        }

        if (sub.kind === 'newHeads' || sub.kind === 'block') { // block is for eth_newBlockFilter
            if (currentVerifiableBlockNumber) {
                let fromBlock = sub.lastPolledBlockNumber ? sub.lastPolledBlockNumber + 1n : currentVerifiableBlockNumber;
                if (sub.kind === 'newHeads') { // For newHeads, we only care if the latest is newer
                    if (!sub.lastPolledBlockNumber) sub.lastPolledBlockNumber = currentVerifiableBlockNumber;
                    if (currentVerifiableBlockNumber <= sub.lastPolledBlockNumber) return []; // No new head yet for eth_subscribe('newHeads')
                }

                for (let blockNum = fromBlock; blockNum <= currentVerifiableBlockNumber; blockNum++) {
                    try {
                        const block = await this.rpcCall('eth_getBlockByNumber', ['0x' + blockNum.toString(16), sub.kind === 'newHeads' /* full block for newHeads */]);
                        if (block && this.activeSubscriptions.has(sub.id)) {
                            if (sub.kind === 'newHeads') results.push(block);
                            else if (sub.kind === 'block') results.push(block.hash); // eth_newBlockFilter returns block hashes
                        }
                    } catch (e) {
                        if (this.config.debug) console.error(`[SubMan] Error fetching block ${blockNum} for sub/filter ${sub.id}:`, e);
                        if (sub.kind === 'block') break; // For block filters, stop if a block in sequence fails
                    }
                }
                if (results.length > 0 && sub.kind === 'newHeads' && !returnResults && this.activeSubscriptions.has(sub.id)) {
                    // For newHeads subscription, emit each block individually or as an array?
                    // EIP-1193 usually implies one result per message for newHeads
                    results.forEach(blockResult => {
                        this.eventEmitter.emit('message', {
                            type: 'eth_subscription',
                            data: { subscription: sub.id, result: blockResult }
                        } as ProviderMessage);
                    });
                    if (this.config.debug) console.log(`[SubMan] Emitted ${results.length} newHead(s) for sub ${sub.id}`);
                }
                sub.lastPolledBlockNumber = currentVerifiableBlockNumber;
            }
        } else if (sub.kind === 'logs' || sub.kind === 'log') { // 'log' is for eth_newFilter(logs)
            const filterParams = { ...(sub.params || {}) };
            if (sub.lastPolledBlockNumber) {
                filterParams.fromBlock = '0x' + (sub.lastPolledBlockNumber + 1n).toString(16);
            } else if (!filterParams.fromBlock && currentVerifiableBlockNumber) {
                filterParams.fromBlock = '0x' + currentVerifiableBlockNumber.toString(16);
            }

            if (currentVerifiableBlockNumber) {
                filterParams.toBlock = '0x' + currentVerifiableBlockNumber.toString(16);
            } else if (!filterParams.toBlock) {
                filterParams.toBlock = 'latest';
            }

            if (filterParams.fromBlock && filterParams.toBlock && filterParams.toBlock !== 'latest' && BigInt(filterParams.fromBlock) > BigInt(filterParams.toBlock)) {
                // Skip if fromBlock is ahead
            } else {
                try {
                    const logs = await this.rpcCall('eth_getLogs', [filterParams]);
                    if (logs && logs.length > 0 && this.activeSubscriptions.has(sub.id)) {
                        results = logs;
                        if (!returnResults) { // For eth_subscribe('logs')
                            this.eventEmitter.emit('message', {
                                type: 'eth_subscription',
                                data: { subscription: sub.id, result: logs } // Usually an array of logs
                            } as ProviderMessage);
                            if (this.config.debug) console.log(`[SubMan] Emitted ${logs.length} logs for sub ${sub.id}`);
                        }
                    }
                    if (filterParams.toBlock !== 'latest') sub.lastPolledBlockNumber = BigInt(filterParams.toBlock);
                    else if (currentVerifiableBlockNumber) sub.lastPolledBlockNumber = currentVerifiableBlockNumber;
                } catch (e) {
                    if (this.config.debug) console.error(`[SubMan] Error fetching logs for sub/filter ${sub.id}:`, e);
                }
            }
        }
        return results;
    }

    private schedulePoll(sub: Subscription, delay: number): void {
        if (!this.activeSubscriptions.has(sub.id) || sub.subscriptionMethod === 'eth_newFilter') return;

        sub.timeoutId = setTimeout(async () => {
            try {
                if (!this.activeSubscriptions.has(sub.id)) return;
                await this.pollSubscriptionAndEmit(sub); // Renamed from pollSubscription for clarity
            } catch (error) {
                console.error(`[SubMan] Unexpected error in poll scheduling for subscription ${sub.id}:`, error);
            }
        }, delay) as any as number;
    }

    // Wrapper for eth_subscribe polling that emits results and reschedules
    private async pollSubscriptionAndEmit(sub: Subscription): Promise<void> {
        if (!this.activeSubscriptions.has(sub.id)) {
            if (this.config.debug) console.log(`[SubMan] Subscription ${sub.id} was unsubscribed. Skipping poll.`);
            if (sub.timeoutId) clearTimeout(sub.timeoutId);
            return;
        }

        if (this.config.debug) {
            console.log(`[SubMan] Polling for eth_subscribe: ${sub.id}, kind: ${sub.kind}`);
        }

        await this.executePollLogic(sub, false); // false means emit results, don't return them

        if (this.activeSubscriptions.has(sub.id)) {
            this.schedulePoll(sub, this.config.pollingInterval || DEFAULT_POLLING_INTERVAL);
        }
    }

    // For eth_unsubscribe (specific to eth_subscribe subscriptions)
    public async unsubscribe(subscriptionId: string): Promise<boolean> {
        const sub = this.activeSubscriptions.get(subscriptionId);
        if (sub && sub.subscriptionMethod === 'eth_subscribe') {
            if (sub.timeoutId) {
                clearTimeout(sub.timeoutId);
            }
            this.activeSubscriptions.delete(subscriptionId);
            if (this.config.debug) {
                console.log(`[SubMan] Unsubscribed (eth_subscribe): ${subscriptionId}`);
            }
            return true;
        }
        if (this.config.debug) {
            console.warn(`[SubMan] Unsubscribe failed: Subscription not found or not an eth_subscribe type for ID ${subscriptionId}`);
        }
        return false;
    }
} 