import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import {
    createProvider,
    explainSimulation,
    WebLLMProvider,
    DEFAULT_WEBLLM_MODEL,
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

    it('exposes a sensible default model id', () => {
        assert.equal(DEFAULT_WEBLLM_MODEL, 'Qwen2.5-Coder-7B-Instruct-q4f16_1-MLC');
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
