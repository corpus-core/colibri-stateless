import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import {
    createProvider,
    explainSimulation,
    WebLLMProvider,
    DEFAULT_WEBLLM_MODEL,
    TSA_EXPLAINER_MODELS,
    shouldDisableThinking,
    resolveModelRecord,
    buildAppConfig,
} from '../dist/index.js';
import { WETH_DEPOSIT_RESULT, TX_PARAMS } from './fixtures.mjs';

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
