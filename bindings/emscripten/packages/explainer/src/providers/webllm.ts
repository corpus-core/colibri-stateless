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

import type { LLMProvider, LLMProviderConfig, ModelProgress } from '../types.js';

/**
 * Default WebLLM model. A code-tuned 7B model that produces solid Solidity
 * explanations but needs a WebGPU device with roughly 5-6 GB of free VRAM.
 * For lower-end devices use a 3B model such as `Llama-3.2-3B-Instruct-q4f16_1-MLC`.
 */
export const DEFAULT_WEBLLM_MODEL = 'Qwen2.5-Coder-7B-Instruct-q4f16_1-MLC';

// Minimal structural typings for the subset of the `@mlc-ai/web-llm` API we use.
// Kept local so the package compiles even when the optional dependency is absent.
interface ChatCompletionLike {
    choices?: { message?: { content?: string } }[];
}

interface MLCEngineLike {
    chat: { completions: { create(request: unknown): Promise<ChatCompletionLike> } };
}

interface WebLLMModule {
    CreateMLCEngine(
        modelId: string,
        engineConfig?: { initProgressCallback?: (report: { progress?: number; text?: string }) => void },
        chatOpts?: { context_window_size?: number },
    ): Promise<MLCEngineLike>;
}

/**
 * Cache of engine instances keyed by `model::contextWindow`, so a model is only
 * downloaded and initialized once per page even across multiple provider
 * instances. The promise is cached (and evicted on failure) to deduplicate
 * concurrent initializations.
 */
const engineCache = new Map<string, Promise<MLCEngineLike>>();

/**
 * Local LLM provider running fully in the browser via WebGPU (WebLLM / MLC).
 * No data leaves the device. Requires a WebGPU-capable browser and the optional
 * `@mlc-ai/web-llm` dependency.
 */
export class WebLLMProvider implements LLMProvider {
    private model: string;
    private maxTokens: number;
    private temperature: number;
    private contextWindowSize?: number;
    private onProgress?: (progress: ModelProgress) => void;
    private injectedEngine?: MLCEngineLike;

    constructor(config: LLMProviderConfig) {
        this.model = config.model || DEFAULT_WEBLLM_MODEL;
        this.maxTokens = config.maxTokens || 1024;
        this.temperature = config.temperature ?? 0.2;
        this.contextWindowSize = config.contextWindowSize;
        this.onProgress = config.onModelProgress;
        this.injectedEngine = config.webllmEngine as MLCEngineLike | undefined;
    }

    async complete(systemPrompt: string, userPrompt: string): Promise<string> {
        const engine = await this.getEngine();

        const res = await engine.chat.completions.create({
            messages: [
                { role: 'system', content: systemPrompt },
                { role: 'user', content: userPrompt },
            ],
            max_tokens: this.maxTokens,
            temperature: this.temperature,
        });

        const content = res.choices?.[0]?.message?.content;
        if (!content) {
            throw new Error('WebLLM returned an empty response');
        }

        return content;
    }

    private async getEngine(): Promise<MLCEngineLike> {
        if (this.injectedEngine) return this.injectedEngine;

        const key = `${this.model}::${this.contextWindowSize ?? 'default'}`;
        let pending = engineCache.get(key);
        if (!pending) {
            pending = this.createEngine();
            engineCache.set(key, pending);
        }

        try {
            return await pending;
        } catch (err) {
            // Allow a later retry after a failed initialization, but only evict
            // the exact promise that failed: a concurrent caller may already have
            // stored a fresh (healthy) initialization under the same key.
            if (engineCache.get(key) === pending) {
                engineCache.delete(key);
            }
            throw err;
        }
    }

    private async createEngine(): Promise<MLCEngineLike> {
        if (typeof navigator === 'undefined' || !(navigator as { gpu?: unknown }).gpu) {
            throw new Error(
                'WebGPU is not available. The "webllm" provider requires a WebGPU-capable browser (e.g. recent Chrome/Edge).',
            );
        }

        let webllm: WebLLMModule;
        try {
            webllm = (await import('@mlc-ai/web-llm')) as unknown as WebLLMModule;
        } catch {
            throw new Error(
                'The optional dependency "@mlc-ai/web-llm" is not installed. Run `npm install @mlc-ai/web-llm` to use the local WebGPU provider.',
            );
        }

        const engineConfig = this.onProgress
            ? {
                initProgressCallback: (report: { progress?: number; text?: string }) =>
                    this.onProgress!({ progress: report.progress ?? 0, text: report.text ?? '' }),
            }
            : undefined;

        const chatOpts = this.contextWindowSize ? { context_window_size: this.contextWindowSize } : undefined;

        return webllm.CreateMLCEngine(this.model, engineConfig, chatOpts);
    }
}
