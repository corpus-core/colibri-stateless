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

import type { ExplainerConfig, LLMProvider } from '../types.js';
import { OpenAIProvider } from './openai.js';
import { AnthropicProvider } from './anthropic.js';
import { WebLLMProvider } from './webllm.js';

/** Create an LLM provider instance from the explainer configuration. */
export function createProvider(config: ExplainerConfig): LLMProvider {
    const providerConfig = {
        apiKey: config.apiKey,
        model: config.model,
        baseUrl: config.baseUrl,
        maxTokens: config.maxTokens,
        temperature: config.temperature,
        contextWindowSize: config.contextWindowSize,
        onModelProgress: config.onModelProgress,
        webllmEngine: config.webllmEngine,
    };

    switch (config.provider) {
        case 'openai':
            return new OpenAIProvider(providerConfig);

        case 'ollama':
            return new OpenAIProvider({
                ...providerConfig,
                baseUrl: config.baseUrl || 'http://localhost:11434',
            });

        case 'anthropic':
            return new AnthropicProvider(providerConfig);

        case 'webllm':
            return new WebLLMProvider(providerConfig);

        default:
            throw new Error(`Unknown LLM provider: ${config.provider}`);
    }
}
