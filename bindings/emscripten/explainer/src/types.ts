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

// -- Simulation result types (aligned with Tenderly response format) --

export interface SimulationLog {
    name?: string;
    inputs?: InputParam[];
    raw: {
        address: string;
        data: string;
        topics: string[];
    };
}

export interface InputParam {
    name: string;
    type: string;
    value: string;
}

export interface TraceEntry {
    from?: string;
    to?: string;
    gas?: string;
    gasUsed?: string;
    input?: string;
    output?: string;
    method?: string;
    decodedInput?: InputParam[];
    decodedOutput?: InputParam[];
    value?: string;
    type?: string;
    traceAddress?: number[];
    subtraces?: number;
}

export interface StateChange {
    address: string;
    slot: string;
    oldValue: string;
    newValue: string;
    preimage?: { input: string; slot: number };
    soltype?: { name: string; type: string };
}

export interface BalanceChange {
    address: string;
    oldBalance: string;
    newBalance: string;
}

export interface AssetChange {
    type: 'Transfer' | 'Mint' | 'Burn';
    from: string;
    to: string;
    amount: string;
    rawAmount: string;
    tokenInfo?: {
        standard: string;
        symbol: string;
        name: string;
        decimals: number;
        contractAddress: string;
    };
}

/**
 * Result of `colibri_simulateTransaction`, aligned with the
 * [Tenderly simulation format](https://docs.tenderly.co/node/guides/simulate-json-rpc).
 *
 * Phase 1 fields (available now): `gasUsed`, `status`, `returnValue`, `logs`.
 * Phase 2 fields (future): `stateChanges`, `balanceChanges`, `assetChanges`, `trace`.
 */
export interface SimulationResult {
    gasUsed: string;
    status: string;
    returnValue: string;
    logs: SimulationLog[];
    stateChanges?: StateChange[];
    balanceChanges?: BalanceChange[];
    assetChanges?: AssetChange[];
    trace?: TraceEntry[];
}

/** Transaction parameters as passed to `colibri_simulateTransaction`. */
export interface TxParams {
    to: string;
    from?: string;
    value?: string;
    data?: string;
    gas?: string;
}

// -- Explainer configuration --

export type LLMProviderType = 'openai' | 'anthropic' | 'ollama';

/** Prompt-related configuration (subset of ExplainerConfig). */
export interface PromptConfig {
    /**
     * Additional context appended to the system prompt.
     * Use this to inject app-specific instructions, e.g.
     * `"This is a DeFi wallet. Focus on user-facing financial impact."`.
     */
    systemPromptInclude?: string;
    /** Desired response language as ISO 639-1 code (e.g. `"de"`, `"es"`). Default: English. */
    language?: string;
}

/** Configuration shared by all LLM provider implementations. */
export interface LLMProviderConfig {
    apiKey?: string;
    model?: string;
    baseUrl?: string;
    maxTokens?: number;
    /** Sampling temperature (0.0 = deterministic, 1.0 = creative). Default: 0.2. */
    temperature?: number;
}

export interface ExplainerConfig extends PromptConfig, LLMProviderConfig {
    /** LLM provider to use. */
    provider: LLMProviderType;
}

// -- LLM provider interface --

export interface LLMProvider {
    complete(systemPrompt: string, userPrompt: string): Promise<string>;
}
