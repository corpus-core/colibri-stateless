import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { register } from 'node:module';
import {
    createProvider,
    explainSimulation,
    WebLLMProvider,
    DEFAULT_WEBLLM_MODEL,
    TSA_EXPLAINER_MODELS,
    shouldDisableThinking,
    resolveModelRecord,
    buildAppConfig,
    isDeadEngineError,
} from '../dist/index.js';
import { WETH_DEPOSIT_RESULT, TX_PARAMS } from './fixtures.mjs';

// `createEngine()` dynamically imports the optional `@mlc-ai/web-llm` package.
// Redirect that specifier at a factory on globalThis so cache eviction and the
// one-shot GPU restart can run in Node, without WebGPU or a model download.
// CI uses Node 20, which exports `register` but not `registerHooks` (22.15+).
await register('./webllm-loader.mjs', import.meta.url);

/**
 * Build a fake WebLLM engine that records the last request and returns a fixed
 * reply. This lets us exercise the provider without downloading a real model.
 */
function makeFakeEngine(reply) {
    const calls = [];
    return {
        calls,
        chat: {
            completions: {
                create: async (req) => {
                    calls.push(req);
                    return { choices: [{ message: { content: reply } }] };
                },
            },
        },
    };
}

/** Reply payload a cached fake engine returns from `chat.completions.create`. */
function chatReply(content) {
    return { choices: [{ message: { content } }] };
}

/**
 * Install `navigator.gpu` for one test. Node's `navigator` is getter-only, so
 * the original property descriptor has to be put back afterwards.
 *
 * @return Restore function
 */
function withWebGpu() {
    const prev = Object.getOwnPropertyDescriptor(globalThis, 'navigator');
    Object.defineProperty(globalThis, 'navigator', {
        configurable: true,
        enumerable: true,
        writable: true,
        value: { gpu: {}, userAgent: 'test' },
    });
    return () => {
        // Node 20 has no global `navigator`, so there is no descriptor to put back.
        if (prev) Object.defineProperty(globalThis, 'navigator', prev);
        else delete globalThis.navigator;
    };
}

/** @return A promise plus its resolve and reject functions */
function deferred() {
    let resolve;
    let reject;
    const promise = new Promise((res, rej) => {
        resolve = res;
        reject = rej;
    });
    return { promise, resolve, reject };
}

/**
 * Point the mocked `CreateMLCEngine` at `factory` until `restore` runs.
 * Each test gets its own factory so an evicted engine from an earlier test
 * cannot increment this test's counters.
 *
 * @param factory - `(model, engineConfig, chatOpts) => engine | Promise<engine>`
 * @return Restore function
 */
function useWebLlmFactory(factory) {
    const restoreGpu = withWebGpu();
    globalThis.__colibriExplainerWebllmFactory = factory;
    return () => {
        globalThis.__colibriExplainerWebllmFactory = undefined;
        restoreGpu();
    };
}

describe('createProvider (webllm)', () => {
    it('creates a WebLLM provider', () => {
        const provider = createProvider({ provider: 'webllm' });
        assert.ok(provider instanceof WebLLMProvider);
        assert.equal(typeof provider.complete, 'function');
    });

    it('defaults to the fine-tuned explainer model', () => {
        assert.equal(DEFAULT_WEBLLM_MODEL, 'colibri-tsa-4b-q4f16_1-MLC');
        assert.equal(TSA_EXPLAINER_MODELS[0].model_id, DEFAULT_WEBLLM_MODEL);
    });
});

describe('fine-tuned model records', () => {
    it('ships the 4B fine-tune with a 16k context', () => {
        const rec = TSA_EXPLAINER_MODELS[0];
        assert.equal(rec.model_id, 'colibri-tsa-4b-q4f16_1-MLC');
        assert.equal(rec.overrides.context_window_size, 16384);
        assert.equal(rec.vram_required_MB, 3900);
    });

    it('every record is complete and reuses a prebuilt Qwen3.5 model library', () => {
        for (const rec of TSA_EXPLAINER_MODELS) {
            assert.match(rec.model, /^https:\/\/huggingface\.co\/corpus-core\//);
            assert.match(rec.model_id, /^colibri-tsa-/);
            assert.match(rec.model_lib, /^Qwen3\.5-\d+B-q4f16_1_cs1k-webgpu\.wasm$/);
            assert.ok(rec.vram_required_MB > 0);
            assert.ok(rec.overrides.context_window_size >= 8192);
            assert.equal(rec.disable_thinking, true);
            // Used as a URL path segment by self-hosted model servers.
            assert.match(rec.weights_version, /^v\d+$/);
        }
    });

    it('resolveModelRecord builds the library URL from the runtime prefix and keeps absolute URLs', () => {
        const webllm = { modelLibURLPrefix: 'https://libs.example/', modelVersion: 'v0_2_84/base' };
        const out = resolveModelRecord(TSA_EXPLAINER_MODELS[0], webllm);
        assert.equal(out.model_lib, 'https://libs.example/v0_2_84/base/Qwen3.5-4B-q4f16_1_cs1k-webgpu.wasm');
        // The source record is not mutated.
        assert.equal(TSA_EXPLAINER_MODELS[0].model_lib, 'Qwen3.5-4B-q4f16_1_cs1k-webgpu.wasm');
        const abs = resolveModelRecord({ ...out, model_lib: 'https://cdn.example/x.wasm' }, webllm);
        assert.equal(abs.model_lib, 'https://cdn.example/x.wasm');
        assert.throws(() => resolveModelRecord(TSA_EXPLAINER_MODELS[0], {}), /modelLibURLPrefix/);
    });

    it('buildAppConfig appends resolved records after the prebuilt list and overrides duplicates', () => {
        const webllm = {
            modelLibURLPrefix: 'https://libs.example/',
            modelVersion: 'v1',
            prebuiltAppConfig: {
                cacheBackend: 'cache',
                model_list: [
                    { model: 'a', model_id: 'Prebuilt-A', model_lib: 'https://x/a.wasm' },
                    { model: 'stale', model_id: 'colibri-tsa-4b-q4f16_1-MLC', model_lib: 'https://x/old.wasm' },
                ],
            },
        };
        const cfg = buildAppConfig(webllm);
        assert.equal(cfg.cacheBackend, 'cache');
        assert.deepEqual(cfg.model_list.map((r) => r.model_id), ['Prebuilt-A', 'colibri-tsa-4b-q4f16_1-MLC']);
        assert.equal(cfg.model_list[1].model, TSA_EXPLAINER_MODELS[0].model);
        assert.equal(cfg.model_list[1].model_lib, 'https://libs.example/v1/Qwen3.5-4B-q4f16_1_cs1k-webgpu.wasm');
        // Works without a prebuilt list too.
        assert.equal(buildAppConfig({ modelLibURLPrefix: 'p/', modelVersion: 'v' }).model_list.length, TSA_EXPLAINER_MODELS.length);
    });

    it('resolveModelRecord leaves a local weight URL untouched (WebLLM appends resolve/main/ itself)', () => {
        const local = { ...TSA_EXPLAINER_MODELS[0], model: 'http://localhost:8787/' };
        const out = resolveModelRecord(local, { modelLibURLPrefix: 'p/', modelVersion: 'v' });
        assert.equal(out.model, 'http://localhost:8787/');
        assert.equal(out.model_lib, 'p/v/Qwen3.5-4B-q4f16_1_cs1k-webgpu.wasm');
        // Custom records replace the built-in ones in the app config.
        const cfg = buildAppConfig({ modelLibURLPrefix: 'p/', modelVersion: 'v', prebuiltAppConfig: { model_list: [] } }, [local]);
        assert.deepEqual(cfg.model_list.map((r) => r.model), ['http://localhost:8787/']);
    });

    it('shouldDisableThinking is on for fine-tunes and prebuilt Qwen3.x, off for others', () => {
        assert.equal(shouldDisableThinking('colibri-tsa-4b-q4f16_1-MLC'), true);
        assert.equal(shouldDisableThinking('Qwen3-4B-q4f16_1-MLC'), true);
        assert.equal(shouldDisableThinking('Qwen3.5-9B-q4f16_1-MLC'), true);
        assert.equal(shouldDisableThinking('Qwen2.5-Coder-7B-Instruct-q4f16_1-MLC'), false);
        assert.equal(shouldDisableThinking('Llama-3.2-3B-Instruct-q4f16_1-MLC'), false);
        assert.equal(shouldDisableThinking('custom', [{ model: 'm', model_id: 'custom', model_lib: 'l', disable_thinking: false }]), false);
    });
});

describe('WebLLMProvider.complete', () => {
    it('maps system/user prompts to chat messages and returns the content', async () => {
        const engine = makeFakeEngine('Deposits 0.1 ETH into WETH.');
        const provider = new WebLLMProvider({ webllmEngine: engine, temperature: 0.5, maxTokens: 256 });

        const out = await provider.complete('SYS', 'USER');

        assert.equal(out, 'Deposits 0.1 ETH into WETH.');
        assert.equal(engine.calls.length, 1);
        const req = engine.calls[0];
        assert.deepEqual(req.messages, [
            { role: 'system', content: 'SYS' },
            { role: 'user', content: 'USER' },
        ]);
        assert.equal(req.temperature, 0.5);
        assert.equal(req.max_tokens, 256);
        // Default model is a Qwen3.5 fine-tune: thinking is switched off.
        assert.deepEqual(req.extra_body, { enable_thinking: false });
    });

    it('sends no extra_body for models that do not think, and honours disableThinking', async () => {
        const engine = makeFakeEngine('ok');
        await new WebLLMProvider({ webllmEngine: engine, model: 'Llama-3.2-3B-Instruct-q4f16_1-MLC' }).complete('s', 'u');
        assert.equal('extra_body' in engine.calls[0], false);
        await new WebLLMProvider({ webllmEngine: engine, model: 'Llama-3.2-3B-Instruct-q4f16_1-MLC', disableThinking: true }).complete('s', 'u');
        assert.deepEqual(engine.calls[1].extra_body, { enable_thinking: false });
        await new WebLLMProvider({ webllmEngine: engine, disableThinking: false }).complete('s', 'u');
        assert.equal('extra_body' in engine.calls[2], false);
    });

    it('reports device loss for an injected engine without unloading or retrying', async () => {
        let creates = 0;
        let unloads = 0;
        const engine = {
            unload: async () => { unloads += 1; },
            chat: {
                completions: {
                    create: async () => {
                        creates += 1;
                        throw new Error('The current Object has already been disposed');
                    },
                },
            },
        };
        await assert.rejects(
            () => new WebLLMProvider({ webllmEngine: engine }).complete('s', 'u'),
            (err) => {
                assert.equal(
                    err.message,
                    'WebGPU device was lost, usually because the GPU ran out of memory. Lower the context window or pick a smaller model, then try again. (The current Object has already been disposed)',
                );
                return true;
            },
        );
        assert.equal(creates, 1);
        assert.equal(unloads, 0);
    });

    it('does not rewrite errors that are not a dead engine', async () => {
        const engine = {
            chat: {
                completions: {
                    create: async () => { throw new Error('context window exceeded'); },
                },
            },
        };
        await assert.rejects(
            () => new WebLLMProvider({ webllmEngine: engine }).complete('s', 'u'),
            (err) => {
                assert.equal(err.message, 'context window exceeded');
                return true;
            },
        );
    });

    it('throws on an empty engine response', async () => {
        const engine = makeFakeEngine('');
        const provider = new WebLLMProvider({ webllmEngine: engine });
        await assert.rejects(() => provider.complete('s', 'u'), /empty response/);
    });

    it('applies default sampling params and preserves temperature 0', async () => {
        let req;
        const engine = {
            chat: { completions: { create: async (r) => { req = r; return { choices: [{ message: { content: 'ok' } }] }; } } },
        };

        await new WebLLMProvider({ webllmEngine: engine }).complete('s', 'u');
        assert.equal(req.temperature, 0.2);
        assert.equal(req.max_tokens, 1024);

        // `temperature: 0` (deterministic) must survive the `?? 0.2` default.
        await new WebLLMProvider({ webllmEngine: engine, temperature: 0 }).complete('s', 'u');
        assert.equal(req.temperature, 0);
    });
});

describe('isDeadEngineError', () => {
    it('matches device-loss failures and ignores ordinary errors', () => {
        assert.equal(isDeadEngineError(new Error('The current Object has already been disposed')), true);
        assert.equal(isDeadEngineError(new Error('Device was lost. Detailed error: [object GPUDeviceLostInfo]')), true);
        assert.equal(isDeadEngineError(new Error('A valid external Instance reference no longer exists.')), true);
        const named = new Error('boom');
        named.name = 'DeviceLostError';
        assert.equal(isDeadEngineError(named), true);
        assert.equal(isDeadEngineError(new Error('WebGPU is not available')), false);
        assert.equal(isDeadEngineError(new Error('WebLLM returned an empty response')), false);
        assert.equal(isDeadEngineError(new Error('context window exceeded')), false);
    });

    it('classifies string values, the device-lost name, and non-errors', () => {
        assert.equal(isDeadEngineError('Device was lost'), true);
        assert.equal(isDeadEngineError('device lost'), true);
        assert.equal(isDeadEngineError(new Error('OBJECT DISPOSED')), true);
        const named = new Error('nope');
        named.name = 'disposed';
        assert.equal(isDeadEngineError(named), true);
        assert.equal(isDeadEngineError(null), false);
        assert.equal(isDeadEngineError(undefined), false);
        // Only Error instances expose a name; a plain object is stringified.
        assert.equal(isDeadEngineError({ message: 'disposed' }), false);
    });
});

describe('WebLLMProvider without an injected engine', () => {
    it('rejects when WebGPU is unavailable', async () => {
        // In Node there is no `navigator.gpu`, so engine creation must fail clearly.
        const provider = new WebLLMProvider({});
        await assert.rejects(() => provider.complete('s', 'u'), /WebGPU is not available/);
    });

    it('keeps failing on retry after a failed initialization', async () => {
        const provider = new WebLLMProvider({ model: 'Some-Model-MLC' });
        await assert.rejects(() => provider.complete('s', 'u'), /WebGPU is not available/);
        await assert.rejects(() => provider.complete('s', 'u'), /WebGPU is not available/);
    });
});

describe('WebLLMProvider GPU recovery for a cached engine', () => {
    it('reloads a dead engine once, unloads it, and reuses the replacement', async () => {
        const stats = { creates: 0, unloads: 0 };
        const restore = useWebLlmFactory(async () => {
            stats.creates += 1;
            const generation = stats.creates;
            return {
                unload: async () => { stats.unloads += 1; },
                chat: {
                    completions: {
                        create: async () => {
                            if (generation === 1) throw new Error('The current Object has already been disposed');
                            return chatReply('recovered');
                        },
                    },
                },
            };
        });
        try {
            const provider = new WebLLMProvider({ model: 'coverage-retry-once' });
            assert.equal(await provider.complete('s', 'u'), 'recovered');
            assert.equal(stats.creates, 2);
            assert.equal(stats.unloads, 1);
            assert.equal(await provider.complete('s', 'u'), 'recovered');
            assert.equal(stats.creates, 2);
            assert.equal(stats.unloads, 1);
        } finally {
            restore();
        }
    });

    it('throws a GPU memory error when the reloaded engine dies too', async () => {
        const stats = { creates: 0, unloads: 0 };
        const restore = useWebLlmFactory(async () => {
            stats.creates += 1;
            const generation = stats.creates;
            return {
                unload: async () => { stats.unloads += 1; },
                chat: {
                    completions: {
                        create: async () => {
                            if (generation === 1) throw new Error('The current Object has already been disposed');
                            throw new Error('Device was lost');
                        },
                    },
                },
            };
        });
        try {
            await assert.rejects(
                () => new WebLLMProvider({ model: 'coverage-retry-twice' }).complete('s', 'u'),
                (err) => {
                    assert.equal(
                        err.message,
                        'WebGPU device was lost, usually because the GPU ran out of memory. Lower the context window or pick a smaller model, then try again. (Device was lost)',
                    );
                    return true;
                },
            );
            assert.equal(stats.creates, 2);
            assert.equal(stats.unloads, 2);
        } finally {
            restore();
        }
    });

    it('does not retry or unload a cached engine for an ordinary completion error', async () => {
        const stats = { creates: 0, unloads: 0 };
        const restore = useWebLlmFactory(async () => {
            stats.creates += 1;
            return {
                unload: async () => { stats.unloads += 1; },
                chat: {
                    completions: {
                        create: async () => { throw new Error('context window exceeded'); },
                    },
                },
            };
        });
        try {
            const provider = new WebLLMProvider({ model: 'coverage-ordinary' });
            for (let i = 0; i < 2; i++) {
                await assert.rejects(
                    () => provider.complete('s', 'u'),
                    (err) => {
                        assert.equal(err.message, 'context window exceeded');
                        return true;
                    },
                );
            }
            assert.equal(stats.creates, 1);
            assert.equal(stats.unloads, 0);
        } finally {
            restore();
        }
    });

    it('rethrows a non-dead error from the single retry and drops both engines', async () => {
        const stats = { creates: 0, unloads: 0 };
        const restore = useWebLlmFactory(async () => {
            stats.creates += 1;
            const generation = stats.creates;
            return {
                unload: async () => { stats.unloads += 1; },
                chat: {
                    completions: {
                        create: async () => {
                            if (generation === 1) throw new Error('Device was lost');
                            throw new Error('context window exceeded');
                        },
                    },
                },
            };
        });
        try {
            await assert.rejects(
                () => new WebLLMProvider({ model: 'coverage-retry-other' }).complete('s', 'u'),
                (err) => {
                    assert.equal(err.message, 'context window exceeded');
                    return true;
                },
            );
            assert.equal(stats.creates, 2);
            assert.equal(stats.unloads, 2);
        } finally {
            restore();
        }
    });

    it('retries once when engine creation reports device loss and does not unload', async () => {
        const stats = { creates: 0, unloads: 0 };
        const restore = useWebLlmFactory(async () => {
            stats.creates += 1;
            if (stats.creates === 1) {
                const err = new Error('device lost during init');
                err.name = 'DeviceLostError';
                throw err;
            }
            return {
                unload: async () => { stats.unloads += 1; },
                chat: {
                    completions: {
                        create: async () => chatReply('booted'),
                    },
                },
            };
        });
        try {
            const provider = new WebLLMProvider({ model: 'coverage-create-dead' });
            assert.equal(await provider.complete('s', 'u'), 'booted');
            assert.equal(stats.creates, 2);
            assert.equal(stats.unloads, 0);
        } finally {
            restore();
        }
    });

    it('continues the retry when unload of the dead engine throws', async () => {
        const stats = { creates: 0, unloads: 0 };
        const restore = useWebLlmFactory(async () => {
            stats.creates += 1;
            const generation = stats.creates;
            return {
                unload: async () => {
                    stats.unloads += 1;
                    if (generation === 1) throw new Error('A valid external Instance reference no longer exists.');
                },
                chat: {
                    completions: {
                        create: async () => {
                            if (generation === 1) throw new Error('disposed object');
                            return chatReply('ok');
                        },
                    },
                },
            };
        });
        try {
            assert.equal(await new WebLLMProvider({ model: 'coverage-unload-throws' }).complete('s', 'u'), 'ok');
            assert.equal(stats.creates, 2);
            assert.equal(stats.unloads, 1);
        } finally {
            restore();
        }
    });

    it('throws a GPU memory error when engine creation keeps failing', async () => {
        const stats = { creates: 0 };
        const restore = useWebLlmFactory(async () => {
            stats.creates += 1;
            const err = new Error('device lost during init');
            err.name = 'DeviceLostError';
            throw err;
        });
        try {
            await assert.rejects(
                () => new WebLLMProvider({ model: 'coverage-create-twice' }).complete('s', 'u'),
                (err) => {
                    assert.equal(
                        err.message,
                        'WebGPU device was lost, usually because the GPU ran out of memory. Lower the context window or pick a smaller model, then try again. (device lost during init)',
                    );
                    return true;
                },
            );
            assert.equal(stats.creates, 2);
        } finally {
            restore();
        }
    });

    it('retries a device-loss error raised while streaming', async () => {
        const stats = { creates: 0, unloads: 0 };
        const restore = useWebLlmFactory(async () => {
            stats.creates += 1;
            const generation = stats.creates;
            return {
                unload: async () => { stats.unloads += 1; },
                chat: {
                    completions: {
                        create: async () => {
                            if (generation === 1) {
                                return (async function* () {
                                    throw new Error('The current Object has already been disposed');
                                })();
                            }
                            return (async function* () {
                                yield { choices: [{ delta: { content: 're' } }] };
                                yield { choices: [{ delta: { content: 'covered' } }] };
                            })();
                        },
                    },
                },
            };
        });
        try {
            const tokens = [];
            const provider = new WebLLMProvider({
                model: 'coverage-stream-retry',
                onToken: (delta) => { tokens.push(delta); },
            });
            assert.equal(await provider.complete('s', 'u'), 'recovered');
            assert.deepEqual(tokens, ['re', 'covered']);
            assert.equal(stats.creates, 2);
            assert.equal(stats.unloads, 1);
        } finally {
            restore();
        }
    });

    it('retries when the dead engine has no unload method', async () => {
        const stats = { creates: 0 };
        const restore = useWebLlmFactory(async () => {
            stats.creates += 1;
            const generation = stats.creates;
            return {
                chat: {
                    completions: {
                        create: async () => {
                            if (generation === 1) throw new Error('Device was lost');
                            return chatReply('ok');
                        },
                    },
                },
            };
        });
        try {
            assert.equal(await new WebLLMProvider({ model: 'coverage-no-unload' }).complete('s', 'u'), 'ok');
            assert.equal(stats.creates, 2);
        } finally {
            restore();
        }
    });
});

describe('WebLLMProvider cache eviction across context windows', () => {
    it('reuses one engine for the same model and context window', async () => {
        const stats = { creates: 0, unloads: 0 };
        const restore = useWebLlmFactory(async () => {
            stats.creates += 1;
            return {
                unload: async () => { stats.unloads += 1; },
                chat: { completions: { create: async () => chatReply('ok') } },
            };
        });
        try {
            const provider = new WebLLMProvider({ model: 'coverage-reuse', contextWindowSize: 1024 });
            assert.equal(await provider.complete('s', 'u'), 'ok');
            assert.equal(await provider.complete('s', 'u'), 'ok');
            assert.equal(stats.creates, 1);
            assert.equal(stats.unloads, 0);
        } finally {
            restore();
        }
    });

    it('unloads a fulfilled engine before loading a different context window', async () => {
        const order = [];
        const restore = useWebLlmFactory(async (_model, _config, chatOpts) => {
            const window = chatOpts.context_window_size;
            order.push(`create-${window}`);
            return {
                unload: async () => { order.push(`unload-${window}`); },
                chat: { completions: { create: async () => chatReply(`w${window}`) } },
            };
        });
        try {
            const model = 'coverage-windows';
            const narrow = new WebLLMProvider({ model, contextWindowSize: 111 });
            const wide = new WebLLMProvider({ model, contextWindowSize: 222 });
            assert.equal(await narrow.complete('s', 'u'), 'w111');
            assert.equal(await wide.complete('s', 'u'), 'w222');
            assert.equal(await narrow.complete('s', 'u'), 'w111');
            assert.deepEqual(order, [
                'create-111',
                'unload-111',
                'create-222',
                'unload-222',
                'create-111',
            ]);
        } finally {
            restore();
        }
    });

    it('does not wait for an in-flight load of another context window', async () => {
        const started = deferred();
        const gate = deferred();
        const stats = { creates111: 0, creates222: 0, unloads111: 0 };
        const restore = useWebLlmFactory(async (_model, _config, chatOpts) => {
            const window = chatOpts.context_window_size;
            if (window === 111) {
                stats.creates111 += 1;
                if (stats.creates111 === 1) {
                    started.resolve();
                    return gate.promise;
                }
                return {
                    chat: { completions: { create: async () => chatReply('again-111') } },
                };
            }
            stats.creates222 += 1;
            return {
                chat: { completions: { create: async () => chatReply('from-222') } },
            };
        });
        const model = 'coverage-inflight';
        const pendingNarrow = new WebLLMProvider({ model, contextWindowSize: 111 }).complete('s', 'u');
        const capturedNarrow = pendingNarrow.then(
            (text) => ({ ok: true, text }),
            (err) => ({ ok: false, err }),
        );
        try {
            await started.promise;
            const wide = new WebLLMProvider({ model, contextWindowSize: 222 }).complete('s', 'u');
            const finished = await Promise.race([
                wide.then((text) => ({ ok: true, text })),
                new Promise((resolve) => setTimeout(() => resolve({ ok: false }), 500)),
            ]);
            assert.equal(finished.ok, true);
            assert.equal(finished.text, 'from-222');
            assert.equal(stats.unloads111, 0);

            gate.resolve({
                unload: async () => { stats.unloads111 += 1; },
                chat: { completions: { create: async () => chatReply('from-111') } },
            });
            const first = await capturedNarrow;
            assert.equal(first.ok, true);
            assert.equal(first.text, 'from-111');
            assert.equal(stats.unloads111, 1);

            // The in-flight entry was dropped before it settled, so a later
            // request for that window has to create a new engine.
            assert.equal(
                await new WebLLMProvider({ model, contextWindowSize: 111 }).complete('s', 'u'),
                'again-111',
            );
            assert.equal(stats.creates111, 2);
            assert.equal(stats.creates222, 1);
        } finally {
            gate.reject(new Error('cancelled'));
            gate.promise.catch(() => undefined);
            capturedNarrow.catch(() => undefined);
            restore();
        }
    });

    it('does not unload an in-flight load that fails, and still loads the other window', async () => {
        const started = deferred();
        const gate = deferred();
        const stats = { creates333: 0, creates444: 0 };
        const restore = useWebLlmFactory(async (_model, _config, chatOpts) => {
            const window = chatOpts.context_window_size;
            if (window === 333) {
                stats.creates333 += 1;
                started.resolve();
                return gate.promise;
            }
            stats.creates444 += 1;
            return {
                unload: async () => { throw new Error('other window must not be unloaded by a failed sibling'); },
                chat: { completions: { create: async () => chatReply('from-444') } },
            };
        });
        const model = 'coverage-inflight-fail';
        const pending = new WebLLMProvider({ model, contextWindowSize: 333 }).complete('s', 'u');
        const captured = pending.then(
            () => { throw new Error('expected the in-flight load to fail'); },
            (err) => err,
        );
        try {
            await started.promise;
            assert.equal(
                await new WebLLMProvider({ model, contextWindowSize: 444 }).complete('s', 'u'),
                'from-444',
            );
            gate.reject(new Error('init failed'));
            const err = await captured;
            assert.equal(err.message, 'init failed');
            assert.equal(stats.creates333, 1);
            assert.equal(stats.creates444, 1);
        } finally {
            gate.reject(new Error('cancelled'));
            gate.promise.catch(() => undefined);
            captured.catch(() => undefined);
            restore();
        }
    });
});

describe('createProvider forwards config to WebLLMProvider', () => {
    it('passes model/temperature/maxTokens and the injected engine through', async () => {
        let req;
        const engine = {
            chat: { completions: { create: async (r) => { req = r; return { choices: [{ message: { content: 'ok' } }] }; } } },
        };
        const provider = createProvider({ provider: 'webllm', webllmEngine: engine, temperature: 0.9, maxTokens: 42 });
        await provider.complete('s', 'u');
        assert.equal(req.temperature, 0.9);
        assert.equal(req.max_tokens, 42);
    });
});

describe('explainSimulation (webllm, injected engine)', () => {
    it('runs the full pipeline locally without any network call', async () => {
        const engine = makeFakeEngine('This transaction wraps 0.1 ETH into WETH.');

        // Guard: no fetch must happen for a local run without enrichment.
        const originalFetch = globalThis.fetch;
        globalThis.fetch = async () => {
            throw new Error('network must not be used by the local webllm provider');
        };

        try {
            const out = await explainSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, {
                provider: 'webllm',
                webllmEngine: engine,
            });
            assert.equal(out, 'This transaction wraps 0.1 ETH into WETH.');
            assert.ok(engine.calls[0].messages[1].content.includes('Transaction Overview'));
        } finally {
            globalThis.fetch = originalFetch;
        }
    });
});
