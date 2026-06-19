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

export type {
    SimulationResult,
    SimulationLog,
    InputParam,
    TraceEntry,
    StorageSlotChange,
    ContractStateChange,
    AccessListEntry,
    TxParams,
    PromptConfig,
    LLMProviderConfig,
    ExplainerConfig,
    LLMProvider,
    LLMProviderType,
    ModelProgress,
    ContractCache,
    VerifiedContract,
    ContractMetadata,
    SolidityStorageLayout,
    SolidityStorageEntry,
    SolidityStorageType,
    DecodedCall,
    DecodedEvent,
    DecodedError,
    ParsedKey,
    ResolvedSlot,
    EnrichedContext,
    EnhancedSimulationResult,
    EnhancedLog,
    EnhancedTraceEntry,
    EnhancedStorageSlotChange,
    EnhancedContractStateChange,
} from './types.js';

export { buildPrompt } from './prompt.js';
export { createProvider } from './providers/index.js';
export { WebLLMProvider, DEFAULT_WEBLLM_MODEL } from './providers/webllm.js';
export { hexToBigInt, weiToEth, formatTokenAmount, formatGas, shortenAddress } from './format.js';
export { lookupAddress, labelAddress } from './known_addresses.js';
export { fetchContractMetadata, fetchCompilationInput } from './sourcify.js';
export { decodeFunctionCall, decodeEventLog, decodeFunctionResult, decodeRevertData } from './decoder.js';
export { parseSlotSource, resolveStorageSlot, resolveDirectSlot } from './storage.js';
export { enrichSimulation, toEnhancedResult } from './enrich.js';
export { compileAndVerify, loadCompiler, getBundledCompiler } from './compiler.js';
export { extractStorageLayout } from './layout.js';
export { getDefaultCache, get_default_cache, cacheGet, cacheSet } from './cache.js';

import type { SimulationResult, TxParams, ExplainerConfig, EnhancedSimulationResult } from './types.js';
import { buildPrompt } from './prompt.js';
import { createProvider } from './providers/index.js';
import { enrichSimulation, toEnhancedResult } from './enrich.js';

/**
 * Explain a transaction simulation result in human-readable language using an LLM.
 *
 * Takes the JSON result of `colibri_simulateTransaction` and produces a
 * natural-language explanation. When `config.chainId` is set, contract metadata
 * is automatically fetched from Sourcify to enrich the prompt with decoded
 * function calls, resolved storage variables, and source code context.
 *
 * @param result - The simulation result from `colibri_simulateTransaction`
 * @param txParams - The original transaction parameters (to, from, value, data)
 * @param config - LLM provider and enrichment configuration
 * @return The LLM-generated explanation string
 *
 * ```typescript
 * const explanation = await explainSimulation(
 *   simulationResult,
 *   { to: '0xC02a...', data: '0xd0e30db0', value: '0x16345785d8a0000' },
 *   { provider: 'openai', apiKey: 'sk-...', model: 'gpt-4o-mini', chainId: 1 }
 * );
 * ```
 */
export async function explainSimulation(
    result: SimulationResult,
    txParams: TxParams,
    config: ExplainerConfig,
): Promise<string> {
    if (!result || typeof result !== 'object') throw new Error('explainSimulation: result is required');
    if (!txParams?.to) throw new Error('explainSimulation: txParams.to is required');
    if (!config?.provider) throw new Error('explainSimulation: config.provider is required');

    const context = config.chainId
        ? await enrichSimulation(result, txParams, config.chainId, {
            sourcifyBaseUrl: config.sourcifyBaseUrl,
            cache: config.cache,
        })
        : undefined;

    const provider = createProvider(config);
    const { systemPrompt, userPrompt } = buildPrompt(result, txParams, config, context);
    return provider.complete(systemPrompt, userPrompt);
}

/**
 * Enhance a simulation result with decoded metadata and an LLM explanation.
 *
 * Returns a single JSON-serializable object that combines the original
 * `colibri_simulateTransaction` output with decoded function calls, resolved
 * storage variables, decoded events/trace, and a natural-language explanation.
 * Ideal for UIs that need both structured data and a human-readable summary.
 *
 * @param result - The simulation result from `colibri_simulateTransaction`
 * @param txParams - The original transaction parameters (to, from, value, data)
 * @param config - LLM provider and enrichment configuration
 * @return Enhanced simulation result with all decoded fields and explanation
 *
 * ```typescript
 * const enhanced = await enhanceSimulation(
 *   simulationResult,
 *   { to: '0xC02a...', data: '0xd0e30db0', value: '0x16345785d8a0000' },
 *   { provider: 'openai', apiKey: 'sk-...', model: 'gpt-4o-mini', chainId: 1 }
 * );
 * // enhanced.explanation -> "This transaction deposits 0.1 ETH into WETH..."
 * // enhanced.decodedCall -> { name: 'deposit', signature: 'deposit()', params: [] }
 * // enhanced.logs[1].decoded -> { name: 'Deposit', params: [...] }
 * // enhanced.stateChanges[0].storage[0].resolved -> { variableName: 'balanceOf', ... }
 * ```
 */
export async function enhanceSimulation(
    result: SimulationResult,
    txParams: TxParams,
    config: ExplainerConfig,
): Promise<EnhancedSimulationResult> {
    if (!result || typeof result !== 'object') throw new Error('enhanceSimulation: result is required');
    if (!txParams?.to) throw new Error('enhanceSimulation: txParams.to is required');
    if (!config?.provider) throw new Error('enhanceSimulation: config.provider is required');

    const context = config.chainId
        ? await enrichSimulation(result, txParams, config.chainId, {
            sourcifyBaseUrl: config.sourcifyBaseUrl,
            cache: config.cache,
        })
        : { contracts: new Map(), resolvedStorage: new Map(), decodedTrace: [], decodedEvents: [] };

    const provider = createProvider(config);
    const { systemPrompt, userPrompt } = buildPrompt(result, txParams, config, context);
    const explanation = await provider.complete(systemPrompt, userPrompt);

    return toEnhancedResult(result, context, explanation);
}
