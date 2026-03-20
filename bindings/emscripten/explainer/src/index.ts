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
    StateChange,
    BalanceChange,
    AssetChange,
    TxParams,
    PromptConfig,
    LLMProviderConfig,
    ExplainerConfig,
    LLMProvider,
    LLMProviderType,
} from './types.js';

export { buildPrompt } from './prompt.js';
export { createProvider } from './providers/index.js';
export { hexToBigInt, weiToEth, formatTokenAmount, formatGas, shortenAddress } from './format.js';
export { lookupAddress, labelAddress } from './known_addresses.js';

import type { SimulationResult, TxParams, ExplainerConfig } from './types.js';
import { buildPrompt } from './prompt.js';
import { createProvider } from './providers/index.js';

/**
 * Explain a transaction simulation result in human-readable language using an LLM.
 *
 * Takes the JSON result of `colibri_simulateTransaction` (or a Tenderly-compatible
 * simulation response) and produces a natural-language explanation.
 *
 * @param result - The simulation result from `colibri_simulateTransaction`
 * @param txParams - The original transaction parameters (to, from, value, data)
 * @param config - LLM provider configuration
 * @return The LLM-generated explanation string
 *
 * ```typescript
 * const explanation = await explainSimulation(
 *   simulationResult,
 *   { to: '0xC02a...', data: '0xd0e30db0', value: '0x16345785d8a0000' },
 *   { provider: 'openai', apiKey: 'sk-...', model: 'gpt-4o-mini' }
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

    const provider = createProvider(config);
    const { systemPrompt, userPrompt } = buildPrompt(result, txParams, config);
    return provider.complete(systemPrompt, userPrompt);
}
