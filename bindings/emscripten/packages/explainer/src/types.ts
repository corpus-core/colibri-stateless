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

/** One storage slot read or written during simulation. */
export interface AccessedStorageSlot {
    slot: string;
    /** Proven pre-state value of the slot (`src_value`). */
    value: string;
    /** Keccak preimage of `slot`, present when the key was hashed during the call. */
    slotSource?: string;
}

export interface AccessListEntry {
    address: string;
    storageKeys?: string[];
    /** keccak256 of the deployed runtime bytecode. Absent / empty-code hash for EOAs. */
    codeHash?: string;
    /**
     * Accessed slots in the same order as `storageKeys`, each with its proven
     * pre-state value. Present only when the simulation request set
     * `state_values`. Slots that were also written stay listed under
     * `stateChanges`.
     */
    storage?: AccessedStorageSlot[];
}

/** Unique JUMPDEST program counters executed by one code address. */
export interface ExecutedPositions {
    /** Code address. On a delegatecall this is the implementation. */
    address: string;
    /** Sorted unique program counters. Hex quantities. */
    pcs: string[];
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
    /**
     * Unique executed JUMPDEST program counters, grouped by code address.
     * Present only when the simulation request set `positions`.
     */
    positions?: ExecutedPositions[];
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
     * Full override for the base system prompt. When set, it *replaces* the
     * built-in analyst prompt (`DEFAULT_SYSTEM_PROMPT`). The language instruction
     * and `systemPromptInclude` are still appended afterwards, then the
     * untrusted-data handling rule (always last, so it wins recency). Leave unset
     * to use the default. Use `DEFAULT_SYSTEM_PROMPT` as a starting point if you
     * only want to tweak it.
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
     * (after comment stripping). Lower this for local models with a small
     * context window. `0` disables the cap. Default: `10000`.
     *
     * When the trace identifies entry functions, those functions and the
     * modifiers and internal calls reachable from them are embedded as Solidity
     * contracts, together with the storage variables of those contracts and the
     * enums and structs the functions reference. Whole definitions are kept
     * until this budget is spent. When no entry can be matched, source files
     * are windowed into the same budget.
     */
    maxSourceChars?: number;
    /**
     * Maximum number of resolved storage reads printed per contract under
     * `## State Reads`. `0` disables the cap. Default: `8`. Reads that could
     * not be named or that already appear in `stateChanges` are omitted before
     * the cap.
     */
    maxStateValues?: number;
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
     * Read contract return data during enrichment. `data` is hex calldata
     * (`0x` + selector + ABI args). Used to resolve ERC-20 `symbol()` and
     * `decimals()` for addresses that are not in `known_addresses.ts`.
     * Return hex return-data, or `null` when the call fails.
     *
     * Successful reads are stored in `cache` under `c4e_{chainId}_{address}`.
     */
    ethCall?: EthCallFn;
    /**
     * Fetch the on-chain runtime bytecode (`eth_getCode`) for a contract.
     * Only invoked when the full `keccak256` bytecode comparison during
     * verification fails: enrichment then strips the Solidity CBOR metadata
     * trailer from both the compiled and the on-chain code and retries with a
     * Sourcify-style partial match. The fetched bytes are re-hashed against
     * the already-verified `codeHash`, so an untrusted RPC cannot inject code.
     *
     * Omit this to keep verification strict (full match only).
     */
    ethGetCode?: EthGetCodeFn;
}

/**
 * Host-supplied contract call used by enrichment.
 *
 * @param to - Contract address
 * @param data - Hex calldata (`0x` + selector + ABI-encoded args)
 * @return Hex return data, or `null` when the call fails
 */
export type EthCallFn = (to: string, data: string) => Promise<string | null>;

/**
 * Host-supplied `eth_getCode` lookup used by enrichment to recover the full
 * on-chain runtime bytecode during partial-match verification.
 *
 * @param address - Contract address
 * @return Hex bytecode (`0x`-prefixed), or `null` when the call fails
 */
export type EthGetCodeFn = (address: string) => Promise<string | null>;

/** ERC-20 `symbol()` / `decimals()` resolved for one address. */
export interface TokenInfo {
    symbol: string;
    /** Token decimals in the range `0..255`. */
    decimals: number;
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
    /**
     * Extractor generation that produced `storageLayout`.
     * Missing or older than the current layout cache version is rebuilt from `sources`.
     */
    layoutVersion?: number;
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
    /** Solidity contract name from Sourcify compilation metadata, when known. */
    contractName?: string | null;
    /**
     * Solidity source-map (`s:l:f:j:m`, ...) of the deployed runtime bytecode,
     * paired with the runtime bytecode itself. Present when the compilation
     * was rerun with `evm.deployedBytecode.sourceMap` because
     * `SimulationResult.positions` referenced this contract.
     */
    sourceMap?: ContractSourceMap | null;
}

/** Deployed runtime bytecode plus its Solidity source map, keyed by source id. */
export interface ContractSourceMap {
    /** Raw source map string from `evm.deployedBytecode.sourceMap`. */
    sourceMap: string;
    /** Runtime bytecode as `0x...`. Used to walk PUSH immediates. */
    runtimeBytecode: string;
    /** Solidity source id → filename, taken from the compilation output. */
    sourceIndex: Map<number, string>;
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
    /**
     * Lowercase address → per-slot resolution of the accessed reads that are
     * NOT part of `stateChanges`. Filled from `SimulationResult.accessList`
     * when the request opted into `state_values`.
     */
    resolvedReads?: Map<string, ResolvedStateRead[]>;
    decodedTrace: (DecodedCall | null)[];
    decodedEvents: (DecodedEvent | null)[];
    /**
     * Lowercase proxy address → lowercase implementation address, taken from
     * `DELEGATECALL` frames. Storage layout and source for a proxy state change
     * come from the implementation.
     */
    implementations?: Map<string, string>;
    /**
     * Lowercase address → ERC-20 symbol and decimals. Filled by
     * `enrichSimulation` from the token cache or from `ethCall`. Known
     * addresses in `known_addresses.ts` are not repeated here.
     */
    tokens?: Map<string, TokenInfo>;
    /**
     * Lowercase code address → set of function or modifier definitions covered
     * by at least one executed `JUMPDEST`. Empty map when no positions were
     * captured or no source-map was available. Callers use these definitions
     * as the seed set for `sliceUsedFunctions`.
     */
    coveredDefinitions?: Map<string, CoveredDefinition[]>;
}

/** One executed slot beyond `stateChanges`. */
export interface ResolvedStateRead {
    /** Raw storage key, matches the corresponding `accessList[].storage[].slot`. */
    slot: string;
    /** Proven pre-state value. */
    value: string;
    /** Resolved variable name and (optional) mapping keys, when a layout matched. */
    resolved?: ResolvedSlot;
}

/**
 * A Solidity function or modifier whose parser range covers a JUMPDEST that
 * ran during the simulation. `contractName` is the Solidity contract (empty
 * for a file-level definition).
 */
export interface CoveredDefinition {
    filename: string;
    contractName: string;
    name: string;
    kind: 'function' | 'modifier';
    start: number;
    end: number;
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

/** Resolved read on one contract, published under `stateReads`. */
export interface EnhancedContractStateReads {
    address: string;
    reads: ResolvedStateRead[];
}

export interface EnhancedTraceEntry extends TraceEntry {
    decoded?: DecodedCall;
}

/**
 * Label → checksummed address mapping produced by `buildPrompt`. The labels
 * are the same display names used in the user prompt's `## Addresses`
 * section (`sender`, `WETH`, `addr_fffe`, …). Hosts use this map to turn
 * `eth://<label>` placeholder links the model emits in Markdown into real
 * explorer URLs, via `resolveAddressLinks`.
 */
export type AddressBook = Record<string, string>;

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
    /**
     * Slots that were only read (part of `accessList`) and are not present in
     * `stateChanges`. Populated when the simulation request set `state_values`
     * and at least one slot could be resolved or carries a proven pre-state
     * value. Same address grouping as `stateChanges`.
     */
    stateReads?: EnhancedContractStateReads[];
    trace?: EnhancedTraceEntry[];
    explanation: string;
    decodedCall?: DecodedCall;
    error?: DecodedError;
}
