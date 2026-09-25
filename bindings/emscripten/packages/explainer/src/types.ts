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
    /** Path of child indices. JSON may use numbers or hex strings (`"0x0"`). */
    traceAddress?: Array<number | string>;
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
    storageKeys?: string[];
    /** keccak256 of the deployed runtime bytecode. Absent / empty-code hash for EOAs. */
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
     * Replaces the base block (`BASE_PROMPT`) only. The format block, the mode
     * block and the untrusted-data rule are still appended, in that order.
     * Leave unset to use the default base.
     */
    systemPrompt?: string;
    /**
     * Additional context appended to the system prompt.
     * Use this to inject app-specific instructions, e.g.
     * `"This is a DeFi wallet. Focus on user-facing financial impact."`.
     */
    systemPromptInclude?: string;
    /** Desired response language as ISO 639-1 code (e.g. `"de"`, `"es"`). Default: English. */
    language?: string;
    /**
     * Maximum number of source-code characters embedded into the prompt
     * (after license-header stripping). Lower this for local models with a
     * small context window. `0` disables the cap and includes every source
     * file in full. Default: `10000`.
     */
    maxSourceChars?: number;
    /**
     * Which line types the model is asked to emit.
     *
     * - `user` — `SUMMARY`, `RISK`, `NOTE`
     * - `developer` — also `STEP`
     *
     * Default: `developer`.
     */
    explainMode?: 'user' | 'developer';
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
     * Streaming callback invoked while the answer is generated. `delta` is the
     * newly produced fragment, `full` the accumulated text so far. Enables live
     * rendering of the response. Currently only the `webllm` provider streams;
     * other providers ignore it and return the full text via the promise.
     */
    onToken?: (delta: string, full: string) => void;
    /**
     * Invoked once per completed explanation line while a WebLLM answer streams.
     * Other providers ignore it; parse the final string with `parseExplanation`.
     */
    onLine?: (line: ExplanationLine) => void;
    /**
     * Aborts an in-flight WebLLM stream between chunks. The engine may still
     * finish the current token. Other providers ignore it.
     */
    abortSignal?: AbortSignal;
    /**
     * Line-protocol mode forwarded to WebLLM so the grammar can omit `STEP`.
     * Same field as `PromptConfig.explainMode`.
     */
    explainMode?: 'user' | 'developer';
    /**
     * Pre-initialized WebLLM engine to reuse across calls (avoids re-downloading
     * the model). Only used by the `webllm` provider. Typed as `unknown` so the
     * core package stays free of a hard dependency on `@mlc-ai/web-llm`.
     */
    webllmEngine?: unknown;
    /**
     * Complete WebLLM `AppConfig` (`{ model_list: ModelRecord[], ... }`) to use
     * instead of the default (prebuilt models + `TSA_EXPLAINER_MODELS`). Only
     * used by the `webllm` provider when it creates the engine itself.
     */
    webllmAppConfig?: unknown;
    /**
     * Custom WebLLM model records (`ModelRecord[]`) that replace
     * `TSA_EXPLAINER_MODELS` when the provider builds its app config, e.g. a
     * fine-tune served from `http://localhost:8787/` before it is published.
     * Ignored when `webllmAppConfig` is given. Only used by the `webllm` provider.
     */
    webllmModelRecords?: unknown[];
    /**
     * Force the WebLLM "no thinking" mode on or off (`extra_body.enable_thinking`).
     * Default: on for the fine-tuned explainer models and prebuilt Qwen3 /
     * Qwen3.5, off otherwise. Only used by the `webllm` provider.
     */
    disableThinking?: boolean;
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
    /**
     * Optional verified `eth_call` used to read `name()`, `symbol()` and
     * `decimals()` for token contracts that are not in the curated list.
     * Return the hex result, or `null` when the call fails.
     */
    ethCall?: (to: string, data: string) => Promise<string | null>;
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
    /** Verified Sourcify contract name, when the compilation input carried one. */
    contractName?: string | null;
}

/** Compilation artifacts returned by the Sourcify v2 `stdJsonInput` endpoint. */
export interface CompilationInput {
    stdJsonInput: Record<string, unknown> | null;
    compilerVersion: string | null;
    contractName: string | null;
    abi: unknown[] | null;
    sources: Record<string, { content: string }> | null;
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

/**
 * A single value packed into a 32-byte storage word. Multiple members share
 * the same slot when the compiler packs them (e.g. `uint112 reserve0` and
 * `uint112 reserve1` at slot 8 of `UniswapV2Pair`).
 */
export interface ResolvedSlotMember {
    variableName: string;
    variableType: string;
    /** Byte offset from the low-order end of the slot word. */
    offset: number;
    /** Width in bytes (1..32). */
    numberOfBytes: number;
}

export interface ResolvedSlot {
    variableName?: string;
    variableType?: string;
    keys?: ParsedKey[];
    baseSlot: number | string;
    raw: string;
    arrayIndex?: number;
    structField?: string;
    /**
     * Packed members that share this slot word. When present, the storage
     * change should be printed per-member (only members whose extracted value
     * changed) instead of dumping the whole 32-byte word under one name.
     */
    members?: ResolvedSlotMember[];
}

export interface EnrichedContext {
    contracts: Map<string, ContractMetadata>;
    decodedCall?: DecodedCall;
    decodedError?: DecodedError;
    resolvedStorage: Map<string, ResolvedSlot[]>;
    decodedTrace: (DecodedCall | null)[];
    decodedEvents: (DecodedEvent | null)[];
    /** Address labels used in the prompt. Absent until `resolveLabels` runs. */
    labels?: LabelTable;
}

/** Where an address label came from. Names without one of these are not shown. */
export type LabelProvenance = 'known' | 'source' | 'self-declared' | 'signer';

/**
 * One address as it appears in the prompt and in the UI.
 * `text` is the exact string the model sees.
 */
export interface AddressLabel {
    /** Lowercase address. */
    address: string;
    provenance: LabelProvenance;
    /** Display name. Collisions inside one transaction get a `#1` / `#2` suffix. */
    name: string;
    /** Prompt rendering, including provenance. */
    text: string;
    symbol?: string;
    decimals?: number;
    /** Where `decimals` came from. Absent when the amount must be printed raw. */
    decimalsSource?: 'known' | 'call';
}

/** JSON-serializable label lookup for one transaction. */
export interface LabelTable {
    /** Keyed by lowercase address. */
    byAddress: Record<string, AddressLabel>;
}

/** One statement of the explanation line protocol. */
export interface ExplanationLine {
    type: 'summary' | 'step' | 'risk' | 'note';
    /** IDs that appear in the prompt. Unresolved refs are omitted. */
    refs: string[];
    text: string;
    /** Set when the line did not match the protocol. Callers should not render it. */
    malformed?: boolean;
    /** Refs the model cited that were not in the prompt. */
    droppedRefs?: string[];
}

// -- Enhanced result types (JSON-serializable, for UI consumption) --

export interface EnhancedLog extends SimulationLog {
    /** Stable id (`l1`, `l2`, …) assigned before any prompt filtering. */
    id: string;
    decoded?: DecodedEvent;
}

export interface EnhancedStorageSlotChange extends StorageSlotChange {
    /** Stable id (`s1`, `s2`, …) shared by every packed member of this slot. */
    id: string;
    resolved?: ResolvedSlot;
}

export interface EnhancedBalanceChange {
    /** Stable id (`b1`, `b2`, …). */
    id: string;
    previousValue: string;
    newValue: string;
}

export interface EnhancedContractStateChange {
    address: string;
    storage?: EnhancedStorageSlotChange[];
    balance?: EnhancedBalanceChange;
}

export interface EnhancedTraceEntry extends TraceEntry {
    /** Stable id (`c1`, `c2`, …) assigned before the prompt's trace limit. */
    id: string;
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
    /** Raw model output, including any malformed lines. */
    explanation: string;
    /** Parsed line protocol. Malformed lines are flagged and unresolved refs are dropped. */
    lines: ExplanationLine[];
    /** Labels used in the prompt, so the UI can map names back to addresses. */
    labels: AddressLabel[];
    decodedCall?: DecodedCall;
    error?: DecodedError;
}
