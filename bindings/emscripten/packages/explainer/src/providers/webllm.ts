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
import { promptRefs } from '../refs.js';
import { buildLineGrammar, LineStreamParser } from '../lines.js';
import { explainerLog } from '../log.js';

/**
 * A WebLLM `ModelRecord` (the shape `@mlc-ai/web-llm` expects in
 * `appConfig.model_list`) plus display metadata used by UIs.
 *
 * `model_lib` may be a bare file name; it is then resolved against the
 * installed WebLLM's `modelLibURLPrefix + modelVersion`, exactly like the
 * prebuilt entries. That keeps our weights on the model library the runtime
 * ships with instead of pinning a URL that goes stale on the next npm bump.
 */
export interface WebLLMModelRecord {
    /** URL of the MLC weight directory (Hugging Face repo). */
    model: string;
    /** Identifier passed to `CreateMLCEngine`. */
    model_id: string;
    /** WASM model library: absolute URL or file name under the prebuilt prefix. */
    model_lib: string;
    vram_required_MB?: number;
    low_resource_required?: boolean;
    overrides?: { context_window_size?: number };
    /** Approximate weight download in GB, for menus. */
    download_gb?: number;
    /** Human-readable label for menus. */
    label?: string;
    /**
     * Version directory of the weights on a self-hosted model server
     * (`<server>/<model_id>/<weights_version>/`). Bumped whenever new weights
     * are published, because browsers cache shards by URL.
     */
    weights_version?: string;
    /**
     * The base model reasons in a `<think>` block by default. When `true`, the
     * provider asks WebLLM to emit an empty thinking block so the answer is
     * returned directly, matching how the fine-tune was trained.
     */
    disable_thinking?: boolean;
}

/**
 * Models fine-tuned by corpus-core for the explainer task (Qwen3.5 family,
 * `q4f16_1`, converted with MLC and hosted on Hugging Face). They reuse the
 * prebuilt model libraries of the corresponding base models, so no custom
 * WASM is needed. The 4B variant is the default; larger variants are added
 * here once they are published.
 */
export const TSA_EXPLAINER_MODELS: readonly WebLLMModelRecord[] = [
    {
        model: 'https://huggingface.co/corpus-core/colibri-tsa-4b-q4f16_1-MLC',
        model_id: 'colibri-tsa-4b-q4f16_1-MLC',
        model_lib: 'Qwen3.5-4B-q4f16_1_cs1k-webgpu.wasm',
        // ~2.4 GB weights + ~1 GB KV cache at 32k (8 attention layers, 4 KV heads).
        vram_required_MB: 4400,
        low_resource_required: false,
        overrides: { context_window_size: 32768 },
        download_gb: 2.4,
        label: 'Colibri TSA 4B (Qwen3.5, fine-tuned)',
        weights_version: 'v1',
        disable_thinking: true,
    },
];

/**
 * Default WebLLM model: the corpus-core fine-tune of Qwen3.5-4B (~2.4 GB
 * download, ~4.4 GB VRAM incl. a 32k context). It was trained on exactly the
 * prompts this package builds. Prebuilt generic models such as
 * `Qwen2.5-Coder-7B-Instruct-q4f16_1-MLC` still work as alternatives.
 */
export const DEFAULT_WEBLLM_MODEL = TSA_EXPLAINER_MODELS[0].model_id;

/**
 * Whether generation for `modelId` should suppress the model's thinking
 * block. True for our fine-tunes (flagged in the record) and for prebuilt
 * Qwen3 / Qwen3.5 models, which otherwise spend the token budget on
 * `<think>` before answering.
 *
 * @param modelId - WebLLM model id
 * @param records - Custom records to consult (default: `TSA_EXPLAINER_MODELS`)
 * @return `true` when `extra_body.enable_thinking: false` should be sent
 */
export function shouldDisableThinking(modelId: string, records: readonly WebLLMModelRecord[] = TSA_EXPLAINER_MODELS): boolean {
    const rec = records.find((r) => r.model_id === modelId);
    if (rec && rec.disable_thinking !== undefined) return rec.disable_thinking;
    return /^Qwen3(\.\d+)?-/i.test(modelId);
}

// Minimal structural typings for the subset of the `@mlc-ai/web-llm` API we use.
// Kept local so the package compiles even when the optional dependency is absent.
interface ChatCompletionLike {
    choices?: { message?: { content?: string } }[];
}

interface ChatCompletionChunkLike {
    choices?: { delta?: { content?: string } }[];
}

interface MLCEngineLike {
    chat: {
        completions: {
            create(request: unknown): Promise<ChatCompletionLike | AsyncIterable<ChatCompletionChunkLike>>;
        };
    };
}

interface AppConfigLike {
    model_list: WebLLMModelRecord[];
    [key: string]: unknown;
}

interface WebLLMModule {
    CreateMLCEngine(
        modelId: string,
        engineConfig?: {
            appConfig?: AppConfigLike;
            initProgressCallback?: (report: { progress?: number; text?: string }) => void;
        },
        chatOpts?: { context_window_size?: number },
    ): Promise<MLCEngineLike>;
    prebuiltAppConfig?: AppConfigLike;
    modelLibURLPrefix?: string;
    modelVersion?: string;
}

/**
 * Resolve `model_lib` file names against the runtime's prebuilt library
 * location. Absolute URLs pass through unchanged.
 *
 * @param record - Model record, possibly with a bare `model_lib` file name
 * @param webllm - Loaded `@mlc-ai/web-llm` module (for prefix + version)
 * @return Record whose `model_lib` is an absolute URL
 */
export function resolveModelRecord(
    record: WebLLMModelRecord,
    webllm: { modelLibURLPrefix?: string; modelVersion?: string },
): WebLLMModelRecord {
    if (/^https?:\/\//i.test(record.model_lib)) return record;
    const prefix = webllm.modelLibURLPrefix;
    const version = webllm.modelVersion;
    if (!prefix || !version) {
        throw new Error(
            `Cannot resolve model library "${record.model_lib}" for ${record.model_id}: the installed @mlc-ai/web-llm exposes no modelLibURLPrefix/modelVersion.`,
        );
    }
    return { ...record, model_lib: `${prefix}${version}/${record.model_lib}` };
}

/**
 * Build the `appConfig` handed to `CreateMLCEngine`: the runtime's prebuilt
 * list plus our fine-tuned records (resolved). Custom records win over
 * prebuilt entries with the same `model_id`.
 *
 * @param webllm - Loaded `@mlc-ai/web-llm` module
 * @param extra - Records to append (default: `TSA_EXPLAINER_MODELS`)
 * @return App config with the merged model list
 */
export function buildAppConfig(webllm: WebLLMModule, extra: readonly WebLLMModelRecord[] = TSA_EXPLAINER_MODELS): AppConfigLike {
    const base = webllm.prebuiltAppConfig ?? { model_list: [] };
    const resolved = extra.map((r) => resolveModelRecord(r, webllm));
    const custom = new Set(resolved.map((r) => r.model_id));
    return {
        ...base,
        model_list: [...base.model_list.filter((r) => !custom.has(r.model_id)), ...resolved],
    };
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
    private onToken?: (delta: string, full: string) => void;
    private onLine?: LLMProviderConfig['onLine'];
    private abortSignal?: AbortSignal;
    private explainMode: 'user' | 'developer';
    private injectedEngine?: MLCEngineLike;
    private appConfig?: AppConfigLike;
    private modelRecords?: WebLLMModelRecord[];
    private disableThinking: boolean;

    constructor(config: LLMProviderConfig) {
        this.model = config.model || DEFAULT_WEBLLM_MODEL;
        this.maxTokens = config.maxTokens || 1024;
        this.temperature = config.temperature ?? 0.2;
        this.contextWindowSize = config.contextWindowSize;
        this.onProgress = config.onModelProgress;
        this.onToken = config.onToken;
        this.onLine = config.onLine;
        this.abortSignal = config.abortSignal;
        this.explainMode = config.explainMode === 'user' ? 'user' : 'developer';
        this.injectedEngine = config.webllmEngine as MLCEngineLike | undefined;
        this.appConfig = config.webllmAppConfig as AppConfigLike | undefined;
        this.modelRecords = config.webllmModelRecords as WebLLMModelRecord[] | undefined;
        this.disableThinking = config.disableThinking ?? shouldDisableThinking(this.model, this.modelRecords ?? TSA_EXPLAINER_MODELS);
    }

    async complete(systemPrompt: string, userPrompt: string): Promise<string> {
        const engine = await this.getEngine();
        const messages = [
            { role: 'system', content: systemPrompt },
            { role: 'user', content: userPrompt },
        ];
        // WebLLM prefixes the reply with an empty `<think>\n\n</think>\n\n`
        // block when `enable_thinking` is false; the fine-tunes were trained
        // with exactly that prefix (Qwen3.5 chat template, non-thinking).
        const extra = this.disableThinking ? { extra_body: { enable_thinking: false } } : {};
        const refs = promptRefs(userPrompt);
        const grammar = buildLineGrammar(refs, this.explainMode);
        const responseFormat = grammar ? { response_format: { type: 'grammar', grammar } } : {};

        // Stream token-by-token when a callback is provided so callers can render
        // the answer live (generation can take 10-20s on consumer hardware).
        if (this.onToken || this.onLine) {
            const stream = (await engine.chat.completions.create({
                messages,
                max_tokens: this.maxTokens,
                temperature: this.temperature,
                stream: true,
                ...extra,
                ...responseFormat,
            })) as AsyncIterable<ChatCompletionChunkLike>;

            const parser = new LineStreamParser(refs);
            let full = '';
            let parsed = 0;
            let dropped = 0;
            let unresolved = 0;
            for await (const chunk of stream) {
                if (this.abortSignal?.aborted) break;
                const delta = chunk.choices?.[0]?.delta?.content;
                if (!delta) continue;
                full += delta;
                this.onToken?.(delta, full);
                for (const line of parser.push(delta)) {
                    if (line.malformed) dropped += 1;
                    else parsed += 1;
                    unresolved += line.droppedRefs?.length ?? 0;
                    this.onLine?.(line);
                }
            }
            for (const line of parser.finish()) {
                if (line.malformed) dropped += 1;
                else parsed += 1;
                unresolved += line.droppedRefs?.length ?? 0;
                this.onLine?.(line);
            }
            explainerLog('info', 'explanation lines', { scope: 'webllm', parsed, dropped, unresolved });
            if (!full) throw new Error('WebLLM returned an empty response');
            return full;
        }

        const res = (await engine.chat.completions.create({
            messages,
            max_tokens: this.maxTokens,
            temperature: this.temperature,
            ...extra,
            ...responseFormat,
        })) as ChatCompletionLike;

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

        // Always pass an app config: the prebuilt list alone does not know our
        // fine-tuned records. A caller-supplied `webllmAppConfig` replaces it;
        // `webllmModelRecords` swaps only the custom records (local test weights).
        const engineConfig: NonNullable<Parameters<WebLLMModule['CreateMLCEngine']>[1]> = {
            appConfig: this.appConfig ?? buildAppConfig(webllm, this.modelRecords ?? TSA_EXPLAINER_MODELS),
        };
        if (this.onProgress) {
            engineConfig.initProgressCallback = (report: { progress?: number; text?: string }) =>
                this.onProgress!({ progress: report.progress ?? 0, text: report.text ?? '' });
        }

        const chatOpts = this.contextWindowSize ? { context_window_size: this.contextWindowSize } : undefined;

        return webllm.CreateMLCEngine(this.model, engineConfig, chatOpts);
    }
}
