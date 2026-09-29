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
        // Weights are ~2.4 GB. A 32k context needed ~1 GB of KV cache
        // (8 attention layers, 4 KV heads), ~4.4 GB in total. The default
        // window is 16k, so that KV term is ~0.5 GB. 3900 MB is this
        // estimate, not a measured value.
        vram_required_MB: 3900,
        low_resource_required: false,
        overrides: { context_window_size: 16384 },
        download_gb: 2.4,
        label: 'Colibri TSA 4B (Qwen3.5, fine-tuned)',
        weights_version: 'v1',
        disable_thinking: true,
    },
];

/**
 * Default WebLLM model: the corpus-core fine-tune of Qwen3.5-4B (~2.4 GB
 * download, ~3.9 GB VRAM incl. a 16k context). It was trained on exactly the
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
/**
 * WebLLM extensions of OpenAI `CompletionUsage`. Present on a non-streaming
 * reply, and on the last streaming chunk when `stream_options.include_usage`
 * is set. `time_to_first_token_s` is engine prefill time, not the wall clock
 * from `complete()` (that also includes model load and shader compile).
 */
interface CompletionUsageLike {
    prompt_tokens?: number;
    completion_tokens?: number;
    total_tokens?: number;
    extra?: {
        /** Seconds from request receipt to the finished reply, inside the engine. */
        e2e_latency_s?: number;
        prefill_tokens_per_s?: number;
        decode_tokens_per_s?: number;
        /** Prefill seconds until the first token, measured by WebLLM. */
        time_to_first_token_s?: number;
        /** Average seconds between output tokens. */
        time_per_output_token_s?: number;
    };
}

interface ChatCompletionLike {
    choices?: { message?: { content?: string }; finish_reason?: string | null }[];
    usage?: CompletionUsageLike;
}

interface ChatCompletionChunkLike {
    choices?: { delta?: { content?: string }; finish_reason?: string | null }[];
    /** `null` on every chunk except the last one, which carries the request totals. */
    usage?: CompletionUsageLike | null;
}

interface MLCEngineLike {
    chat: {
        completions: {
            create(request: unknown): Promise<ChatCompletionLike | AsyncIterable<ChatCompletionChunkLike>>;
        };
    };
    /** Release the WebGPU device and TVM objects. Absent on injected test doubles. */
    unload?(): Promise<void>;
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
            initProgressCallback?: (report: { progress?: number; text?: string; timeElapsed?: number }) => void;
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
 * instances. The promise is cached (and evicted on failure, or when the GPU
 * device is lost) to deduplicate concurrent initializations. Loading a
 * different key unloads the others first, so two context windows are not
 * resident together.
 */
const engineCache = new Map<string, Promise<MLCEngineLike>>();

/**
 * Whether `err` means the WebGPU device or the WebLLM engine is already dead.
 *
 * WebLLM logs `Device was lost` and then disposes its TVM objects. The next
 * `resetChat` throws `The current Object has already been disposed`, and
 * `unload()` can throw `A valid external Instance reference no longer exists`.
 *
 * @param err - Rejection from engine creation or `chat.completions.create`
 * @return `true` when the cached engine should be dropped and loaded once more
 */
export function isDeadEngineError(err: unknown): boolean {
    const name = err instanceof Error ? err.name : '';
    const message = err instanceof Error ? err.message : String(err ?? '');
    if (name === 'DeviceLostError') return true;
    return /disposed|device was lost|device lost|external instance reference/i.test(`${name} ${message}`);
}

/**
 * User-facing message after a device-loss attempt has failed.
 *
 * @param err - The error from the failed attempt
 * @return Message that includes the original detail
 */
function gpuMemoryMessage(err: unknown): string {
    const detail = err instanceof Error ? err.message : String(err ?? '');
    return `WebGPU device was lost, usually because the GPU ran out of memory. Lower the context window or pick a smaller model, then try again. (${detail})`;
}

/**
 * Release a WebLLM engine. A missing or already-dead `unload` is ignored:
 * WebLLM's own `device.lost` handler may have disposed the instance already.
 *
 * @param engine - Engine to unload
 */
async function releaseEngine(engine: MLCEngineLike): Promise<void> {
    if (typeof engine.unload !== 'function') return;
    try {
        await engine.unload();
    } catch {
        // The device-lost handler may already have disposed the instance.
    }
}

/**
 * Read a promise that has already fulfilled, without waiting for one that has not.
 * One microtask is enough: a fulfilled promise queues its reaction before this
 * await. Waiting on a still-pending load would deadlock when that load is queued
 * behind the caller.
 *
 * @param pending - Promise to inspect
 * @return The value when `pending` is already fulfilled, otherwise `undefined`
 */
async function fulfilledNow<T>(pending: Promise<T>): Promise<T | undefined> {
    // A local `let` assigned only inside `then` stays narrowed to its initializer
    // across `await`, so the result lives on an object.
    const box: { ok: boolean; value?: T } = { ok: false };
    pending.then(
        (v) => {
            box.value = v;
            box.ok = true;
        },
        () => {
            box.ok = false;
        },
    );
    await Promise.resolve();
    return box.ok ? box.value : undefined;
}

/**
 * Format a finite number, or `n/a` when WebLLM omitted the field or divided by zero.
 *
 * @param value - Measurement from `CompletionUsage` or a wall-clock sample
 * @param digits - Digits after the decimal point
 * @return The fixed-point text, or `n/a`
 */
function fmtNum(value: number | undefined, digits: number): string {
    return typeof value === 'number' && Number.isFinite(value) ? value.toFixed(digits) : 'n/a';
}

/**
 * Format a measurement with its unit. A missing value stays `n/a`, so the unit
 * is not glued on (`n/as`).
 *
 * @param value - Measurement to format
 * @param digits - Digits after the decimal point
 * @param unit - Suffix such as `s` or `ms`
 * @return Fixed-point text plus unit, or `n/a`
 */
function fmtUnit(value: number | undefined, digits: number, unit: string): string {
    const text = fmtNum(value, digits);
    return text === 'n/a' ? text : `${text}${unit}`;
}

/**
 * Collapse shard counters inside a WebLLM init-progress text so
 * `Loading model from cache[50/200]` stays one phase.
 *
 * @param text - `InitProgressReport.text`
 * @return Stable phase name
 */
function initPhaseName(text: string): string {
    return text.replace(/\[[^\]]*\]/g, '').replace(/\s+/g, ' ').trim();
}

/**
 * Where the engine for this completion came from. `cold` is a cache miss
 * (download, WASM compile, GPU pipeline). `cached` may still be loading;
 * `engine_wait` is the time actually spent waiting.
 */
type EngineSource = 'injected' | 'cached' | 'cold';

/**
 * Print one line of generation statistics. WebLLM's `time_to_first_token_s`
 * is prefill inside the engine. `engine_wait` is everything before
 * `chat.completions.create` (weight fetch, shader compile). `ttft_wall` is
 * from that call until the first content delta. `to_first_token` is the sum,
 * which is the gap a cold start shows the user.
 *
 * @param info - Tokens, rates, and wall-clock samples for one completion
 */
function logWebllmGeneration(info: {
    model: string;
    contextWindow: string;
    maxTokens: number;
    promptChars: number;
    engine: EngineSource;
    engineWaitMs: number;
    firstTokenMs?: number;
    generateMs: number;
    finish?: string;
    usage?: CompletionUsageLike;
}): void {
    const u = info.usage;
    const extra = u?.extra;
    const toFirst = info.firstTokenMs === undefined ? undefined : info.engineWaitMs + info.firstTokenMs;
    const fill =
        typeof u?.prompt_tokens === 'number' && /^\d+$/.test(info.contextWindow)
            ? `${u.prompt_tokens}/${info.contextWindow}`
            : 'n/a';
    console.log(
        `[webllm] prompt=${u?.prompt_tokens ?? 'n/a'} completion=${u?.completion_tokens ?? 'n/a'} ` +
        `total=${u?.total_tokens ?? 'n/a'} ` +
        `prefill=${fmtNum(extra?.prefill_tokens_per_s, 1)} tok/s ` +
        `decode=${fmtNum(extra?.decode_tokens_per_s, 1)} tok/s ` +
        `ttft=${fmtUnit(extra?.time_to_first_token_s, 3, 's')} ` +
        `e2e=${fmtUnit(extra?.e2e_latency_s, 3, 's')} ` +
        `per_token=${fmtUnit(extra?.time_per_output_token_s, 4, 's')} ` +
        `context_window=${info.contextWindow} fill=${fill} ` +
        `max_tokens=${info.maxTokens} prompt_chars=${info.promptChars} ` +
        `engine=${info.engine} engine_wait=${fmtUnit(info.engineWaitMs, 0, 'ms')} ` +
        `ttft_wall=${fmtUnit(info.firstTokenMs, 0, 'ms')} ` +
        `to_first_token=${fmtUnit(toFirst, 0, 'ms')} ` +
        `generate=${fmtUnit(info.generateMs, 0, 'ms')} ` +
        `finish=${info.finish ?? 'n/a'} model=${info.model}`,
    );
}

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
        this.injectedEngine = config.webllmEngine as MLCEngineLike | undefined;
        this.appConfig = config.webllmAppConfig as AppConfigLike | undefined;
        this.modelRecords = config.webllmModelRecords as WebLLMModelRecord[] | undefined;
        this.disableThinking = config.disableThinking ?? shouldDisableThinking(this.model, this.modelRecords ?? TSA_EXPLAINER_MODELS);
    }

    /**
     * Generate a completion. If the GPU device was lost, drop the cached engine,
     * unload it, and try once more. An injected engine is not reloaded.
     *
     * @param systemPrompt - System message
     * @param userPrompt - User message
     * @return Generated text
     */
    async complete(systemPrompt: string, userPrompt: string): Promise<string> {
        const run = () => this.runCompletion(systemPrompt, userPrompt);
        if (this.injectedEngine) {
            try {
                return await run();
            } catch (err) {
                if (!isDeadEngineError(err)) throw err;
                throw new Error(gpuMemoryMessage(err));
            }
        }

        try {
            return await run();
        } catch (err) {
            if (!isDeadEngineError(err)) throw err;
            await this.evictCachedEngine(this.cacheKey());
            try {
                return await run();
            } catch (retryErr) {
                await this.evictCachedEngine(this.cacheKey());
                if (isDeadEngineError(retryErr)) throw new Error(gpuMemoryMessage(retryErr));
                throw retryErr;
            }
        }
    }

    /**
     * Run one completion against the current engine, streaming when `onToken` is set.
     *
     * @param systemPrompt - System message
     * @param userPrompt - User message
     * @return Generated text
     */
    private async runCompletion(systemPrompt: string, userPrompt: string): Promise<string> {
        // Read the cache before `getEngine` inserts this load, so a miss stays `cold`.
        const engineSource: EngineSource = this.injectedEngine
            ? 'injected'
            : engineCache.has(this.cacheKey())
                ? 'cached'
                : 'cold';
        const engineWaitStart = performance.now();
        const engine = await this.getEngine();
        const engineWaitMs = performance.now() - engineWaitStart;
        const messages = [
            { role: 'system', content: systemPrompt },
            { role: 'user', content: userPrompt },
        ];
        // WebLLM prefixes the reply with an empty `<think>\n\n</think>\n\n`
        // block when `enable_thinking` is false; the fine-tunes were trained
        // with exactly that prefix (Qwen3.5 chat template, non-thinking).
        const extra = this.disableThinking ? { extra_body: { enable_thinking: false } } : {};
        const stats = {
            model: this.model,
            contextWindow: this.contextWindowLabel(),
            maxTokens: this.maxTokens,
            promptChars: systemPrompt.length + userPrompt.length,
            engine: engineSource,
            engineWaitMs,
        };

        // Stream token-by-token when a callback is provided so callers can render
        // the answer live (generation can take 10-20s on consumer hardware).
        // `stream_options` is rejected unless `stream` is true (InvalidStreamOptionsError).
        // Non-streaming replies already include `usage`.
        if (this.onToken) {
            const genStart = performance.now();
            let firstTokenMs: number | undefined;
            let usage: CompletionUsageLike | undefined;
            let finish: string | undefined;
            let full = '';
            try {
                const stream = (await engine.chat.completions.create({
                    messages,
                    max_tokens: this.maxTokens,
                    temperature: this.temperature,
                    stream: true,
                    stream_options: { include_usage: true },
                    ...extra,
                })) as AsyncIterable<ChatCompletionChunkLike>;

                for await (const chunk of stream) {
                    if (chunk.usage) usage = chunk.usage;
                    const reason = chunk.choices?.[0]?.finish_reason;
                    if (reason) finish = reason;
                    const delta = chunk.choices?.[0]?.delta?.content;
                    if (delta) {
                        if (firstTokenMs === undefined) firstTokenMs = performance.now() - genStart;
                        full += delta;
                        this.onToken(delta, full);
                    }
                }
                if (!full) throw new Error('WebLLM returned an empty response');
                return full;
            } finally {
                logWebllmGeneration({
                    ...stats,
                    firstTokenMs,
                    generateMs: performance.now() - genStart,
                    finish,
                    usage,
                });
            }
        }

        const genStart = performance.now();
        let usage: CompletionUsageLike | undefined;
        let finish: string | undefined;
        let content = '';
        try {
            const res = (await engine.chat.completions.create({
                messages,
                max_tokens: this.maxTokens,
                temperature: this.temperature,
                ...extra,
            })) as ChatCompletionLike;
            usage = res.usage;
            finish = res.choices?.[0]?.finish_reason ?? undefined;
            content = res.choices?.[0]?.message?.content ?? '';
            if (!content) throw new Error('WebLLM returned an empty response');
            return content;
        } finally {
            logWebllmGeneration({
                ...stats,
                generateMs: performance.now() - genStart,
                finish,
                usage,
            });
        }
    }

    /**
     * Context window this load will use. An explicit `contextWindowSize` wins
     * over the model record. The fine-tune record sets 16384; a prebuilt model
     * with no record here keeps WebLLM's own default (`model-default`).
     *
     * @return Window size in tokens, or `model-default`
     */
    private contextWindowLabel(): string {
        if (this.contextWindowSize !== undefined) return String(this.contextWindowSize);
        const records = this.appConfig?.model_list ?? this.modelRecords ?? TSA_EXPLAINER_MODELS;
        const size = records.find((r) => r.model_id === this.model)?.overrides?.context_window_size;
        return size !== undefined ? String(size) : 'model-default';
    }

    /**
     * Cache key for this provider's model and context window.
     *
     * @return Key into `engineCache`
     */
    private cacheKey(): string {
        return `${this.model}::${this.contextWindowSize ?? 'default'}`;
    }

    /**
     * Remove `key` from the cache and unload its engine when it has already loaded.
     * A load that is still in flight is unloaded when it settles, without waiting:
     * that promise may be queued behind the caller.
     *
     * @param key - Cache key to drop
     */
    private async evictCachedEngine(key: string): Promise<void> {
        const pending = engineCache.get(key);
        if (!pending) return;
        engineCache.delete(key);
        const engine = await fulfilledNow(pending);
        if (engine) {
            await releaseEngine(engine);
            return;
        }
        void pending.then((loaded) => releaseEngine(loaded)).catch(() => undefined);
    }

    /**
     * Unload every cached engine except `keepKey`, so two context windows are
     * not resident at once.
     *
     * @param keepKey - Cache key that must stay
     */
    private async evictOtherEngines(keepKey: string): Promise<void> {
        for (const key of [...engineCache.keys()]) {
            if (key !== keepKey) await this.evictCachedEngine(key);
        }
    }

    /**
     * Return the injected engine or the cached one, creating it on a miss.
     * The new promise is stored before the first await so concurrent callers
     * share one initialization.
     *
     * @return Loaded engine
     */
    private async getEngine(): Promise<MLCEngineLike> {
        if (this.injectedEngine) return this.injectedEngine;

        const key = this.cacheKey();
        let pending = engineCache.get(key);
        if (!pending) {
            pending = this.loadEngine(key);
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

    /**
     * Unload any other cached engine, then create this one.
     *
     * @param key - Cache key reserved for this load
     * @return Loaded engine
     */
    private async loadEngine(key: string): Promise<MLCEngineLike> {
        await this.evictOtherEngines(key);
        return this.createEngine();
    }

    /**
     * Create a WebLLM engine for this provider's model. `contextWindowSize`,
     * when set, overrides the context window on the model record.
     *
     * @return Loaded engine
     */
    private async createEngine(): Promise<MLCEngineLike> {
        if (typeof navigator === 'undefined' || !(navigator as { gpu?: unknown }).gpu) {
            throw new Error(
                'WebGPU is not available. The "webllm" provider requires a WebGPU-capable browser (e.g. recent Chrome/Edge).',
            );
        }

        const loadStarted = performance.now();
        let webllm: WebLLMModule;
        try {
            webllm = (await import('@mlc-ai/web-llm')) as unknown as WebLLMModule;
        } catch {
            throw new Error(
                'The optional dependency "@mlc-ai/web-llm" is not installed. Run `npm install @mlc-ai/web-llm` to use the local WebGPU provider.',
            );
        }
        const importMs = performance.now() - loadStarted;

        // Always pass an app config: the prebuilt list alone does not know our
        // fine-tuned records. A caller-supplied `webllmAppConfig` replaces it;
        // `webllmModelRecords` swaps only the custom records (local test weights).
        const engineConfig: NonNullable<Parameters<WebLLMModule['CreateMLCEngine']>[1]> = {
            appConfig: this.appConfig ?? buildAppConfig(webllm, this.modelRecords ?? TSA_EXPLAINER_MODELS),
        };
        // Shard counters (`cache[50/200]`) stay one phase. The slow ones on a
        // weak laptop are the weight fetch and `Loading GPU shader modules`.
        let phase = '';
        let phaseAt = performance.now();
        let phaseProgress = 0;
        /**
         * Log the init phase that just ended.
         *
         * @param now - `performance.now()` at the phase boundary
         */
        const logPhase = (now: number) => {
            if (!phase) return;
            console.log(
                `[webllm] init ${fmtNum(now - phaseAt, 0)}ms ${(phaseProgress * 100).toFixed(0)}% "${phase}"`,
            );
        };
        engineConfig.initProgressCallback = (report) => {
            const text = report.text ?? '';
            const name = initPhaseName(text);
            const now = performance.now();
            if (name && name !== phase) {
                // WebLLM fetches mlc-chat-config and the WASM library before the
                // first progress report. That gap is not a named phase.
                if (!phase) {
                    console.log(
                        `[webllm] init ${fmtNum(now - phaseAt, 0)}ms before first progress (config, wasm, gpu detect)`,
                    );
                } else {
                    logPhase(now);
                }
                phase = name;
                phaseAt = now;
            }
            if (typeof report.progress === 'number') phaseProgress = report.progress;
            if (this.onProgress) this.onProgress({ progress: report.progress ?? 0, text });
        };

        const chatOpts = this.contextWindowSize ? { context_window_size: this.contextWindowSize } : undefined;
        const contextWindow = this.contextWindowLabel();

        let engine: MLCEngineLike;
        try {
            engine = await webllm.CreateMLCEngine(this.model, engineConfig, chatOpts);
        } catch (err) {
            logPhase(performance.now());
            console.log(
                `[webllm] engine init failed after ${fmtNum(performance.now() - loadStarted, 0)}ms ` +
                `import=${fmtNum(importMs, 0)}ms model=${this.model} context_window=${contextWindow}`,
            );
            throw err;
        }
        logPhase(performance.now());
        console.log(
            `[webllm] engine ready ${fmtNum(performance.now() - loadStarted, 0)}ms ` +
            `import=${fmtNum(importMs, 0)}ms model=${this.model} context_window=${contextWindow}`,
        );
        return engine;
    }
}
