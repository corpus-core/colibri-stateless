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

// -- Simulation result types (aligned with C-core output format) --

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
    value?: string;
    type?: string;
    traceAddress?: number[];
    subtraces?: string;
}

export interface StorageSlotChange {
    slot: string;
    previousValue: string;
    newValue: string;
    slotSource?: string;
}

export interface ContractStateChange {
    address: string;
    storage?: StorageSlotChange[];
    balance?: { previousValue: string; newValue: string };
}

export interface AccessListEntry {
    address: string;
    codeHash?: string;
}

/**
 * Result of `colibri_simulateTransaction`.
 * Matches the C-core output format with hierarchical stateChanges
 * grouped per contract address.
 */
export interface SimulationResult {
    gasUsed: string;
    status: string;
    returnValue: string;
    logs: SimulationLog[];
    stateChanges?: ContractStateChange[];
    trace?: TraceEntry[];
    accessList?: AccessListEntry[];
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

export type LLMProviderType = 'openai' | 'anthropic' | 'ollama' | 'webllm';

/** Progress information emitted while a local model is being downloaded/initialized. */
export interface ModelProgress {
    /** Loading progress in the range `[0, 1]`. */
    progress: number;
    /** Human-readable status text (e.g. cache/download phase). */
    text: string;
}

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
    /**
     * Maximum number of source-code characters embedded into the prompt.
     * Lower this for local models with a small context window. Default: `10000`.
     */
    maxSourceChars?: number;
}

/** Configuration shared by all LLM provider implementations. */
export interface LLMProviderConfig {
    apiKey?: string;
    model?: string;
    baseUrl?: string;
    maxTokens?: number;
    /** Sampling temperature (0.0 = deterministic, 1.0 = creative). Default: 0.2. */
    temperature?: number;
    /**
     * Override the model's context window size (in tokens). Only used by the
     * local `webllm` provider; many prebuilt WebLLM models default to 4096.
     */
    contextWindowSize?: number;
    /**
     * Progress callback for local model download/initialization.
     * Only invoked by the `webllm` provider.
     */
    onModelProgress?: (progress: ModelProgress) => void;
    /**
     * Pre-initialized WebLLM engine to reuse across calls (avoids re-downloading
     * the model). Only used by the `webllm` provider. Typed as `unknown` so the
     * core package stays free of a hard dependency on `@mlc-ai/web-llm`.
     */
    webllmEngine?: unknown;
}

export interface ExplainerConfig extends PromptConfig, LLMProviderConfig {
    /** LLM provider to use. */
    provider: LLMProviderType;
    /** Chain ID for Sourcify lookups. Enables automatic contract metadata enrichment. */
    chainId?: number;
    /** Base URL for a self-hosted Sourcify instance. Default: `https://sourcify.dev/server`. */
    sourcifyBaseUrl?: string;
    /** Custom cache implementation. Uses localStorage (browser), fs (Node.js), or in-memory fallback by default. */
    cache?: ContractCache;
}

/** Persistent cache for verified contract metadata, keyed by `codeHash`. */
export interface ContractCache {
    get(key: string): Promise<string | null>;
    set(key: string, value: string): Promise<void>;
}

/** Verified and cached contract metadata. Stored as JSON in the cache. */
export interface VerifiedContract {
    abi: unknown[];
    storageLayout: SolidityStorageLayout | null;
    sources: Record<string, { content: string }>;
    compilerVersion: string;
    contractName: string;
}

// -- LLM provider interface --

export interface LLMProvider {
    complete(systemPrompt: string, userPrompt: string): Promise<string>;
}

// -- Sourcify / enrichment types --

export interface SolidityStorageEntry {
    slot: string;
    type: string;
    astId: number;
    label: string;
    offset: number;
    contract: string;
}

export interface SolidityStorageType {
    label: string;
    encoding: string;
    numberOfBytes: string;
    key?: string;
    value?: string;
    base?: string;
    members?: SolidityStorageEntry[];
}

export interface SolidityStorageLayout {
    storage: SolidityStorageEntry[];
    types: Record<string, SolidityStorageType> | null;
}

export interface ContractMetadata {
    abi: unknown[] | null;
    sources: Record<string, { content: string }> | null;
    storageLayout: SolidityStorageLayout | null;
}

export interface DecodedCall {
    name: string;
    signature: string;
    params: { name: string; type: string; value: string }[];
}

export interface DecodedEvent {
    name: string;
    signature: string;
    params: { name: string; type: string; value: string; indexed: boolean }[];
}

export interface DecodedError {
    name: string;
    signature: string;
    params: { name: string; type: string; value: string }[];
    reason?: string;
}

export interface ParsedKey {
    type: 'address' | 'uint256' | 'bytes32' | 'unknown';
    value: string;
}

export interface ResolvedSlot {
    variableName?: string;
    variableType?: string;
    keys?: ParsedKey[];
    baseSlot: number | string;
    raw: string;
    arrayIndex?: number;
    structField?: string;
}

export interface EnrichedContext {
    contracts: Map<string, ContractMetadata>;
    decodedCall?: DecodedCall;
    decodedError?: DecodedError;
    resolvedStorage: Map<string, ResolvedSlot[]>;
    decodedTrace: (DecodedCall | null)[];
    decodedEvents: (DecodedEvent | null)[];
}

// -- Enhanced result types (JSON-serializable, for UI consumption) --

export interface EnhancedLog extends SimulationLog {
    decoded?: DecodedEvent;
}

export interface EnhancedStorageSlotChange extends StorageSlotChange {
    resolved?: ResolvedSlot;
}

export interface EnhancedContractStateChange {
    address: string;
    storage?: EnhancedStorageSlotChange[];
    balance?: { previousValue: string; newValue: string };
}

export interface EnhancedTraceEntry extends TraceEntry {
    decoded?: DecodedCall;
}

/**
 * The original `SimulationResult` enriched with decoded metadata and
 * a natural-language explanation. All fields are JSON-serializable,
 * making this suitable for direct use in UIs or APIs.
 */
export interface EnhancedSimulationResult {
    gasUsed: string;
    status: string;
    returnValue: string;
    logs: EnhancedLog[];
    stateChanges?: EnhancedContractStateChange[];
    trace?: EnhancedTraceEntry[];
    explanation: string;
    decodedCall?: DecodedCall;
    error?: DecodedError;
}
