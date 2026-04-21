import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { explainSimulation, enhanceSimulation, createProvider, buildPrompt } from '../dist/index.js';
import { WETH_DEPOSIT_RESULT, TX_PARAMS, WETH_ABI } from './fixtures.mjs';

describe('createProvider', () => {
    it('creates an OpenAI provider', () => {
        const provider = createProvider({ provider: 'openai', apiKey: 'test' });
        assert.ok(provider);
        assert.ok(typeof provider.complete === 'function');
    });

    it('creates an Anthropic provider', () => {
        const provider = createProvider({ provider: 'anthropic', apiKey: 'test' });
        assert.ok(provider);
        assert.ok(typeof provider.complete === 'function');
    });

    it('creates an Ollama provider', () => {
        const provider = createProvider({ provider: 'ollama' });
        assert.ok(provider);
        assert.ok(typeof provider.complete === 'function');
    });

    it('throws on unknown provider', () => {
        assert.throws(
            () => createProvider({ provider: 'unknown' }),
            /Unknown LLM provider/
        );
    });
});

describe('explainSimulation', () => {
    it('calls provider with built prompt and returns response (no enrichment)', async () => {
        const mockExplanation = 'This transaction deposits 0.1 ETH into WETH.';

        const originalFetch = globalThis.fetch;
        globalThis.fetch = async (_url, opts) => {
            const body = JSON.parse(opts?.body);
            assert.equal(body.model, 'gpt-4o-mini');
            assert.ok(body.messages[0].content.includes('blockchain transaction analyst'));
            assert.ok(body.messages[1].content.includes('WETH'));
            assert.equal(body.temperature, 0.2);

            return new Response(JSON.stringify({
                choices: [{ message: { content: mockExplanation } }],
            }), { status: 200, headers: { 'Content-Type': 'application/json' } });
        };

        try {
            const result = await explainSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, {
                provider: 'openai',
                apiKey: 'test-key',
                model: 'gpt-4o-mini',
            });

            assert.equal(result, mockExplanation);
        } finally {
            globalThis.fetch = originalFetch;
        }
    });

    it('propagates API errors', async () => {
        const originalFetch = globalThis.fetch;
        globalThis.fetch = async () => {
            return new Response('Internal Server Error', { status: 500 });
        };

        try {
            await assert.rejects(
                () => explainSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, {
                    provider: 'openai',
                    apiKey: 'test-key',
                }),
                /OpenAI API error 500/
            );
        } finally {
            globalThis.fetch = originalFetch;
        }
    });

    it('validates required inputs', async () => {
        await assert.rejects(
            () => explainSimulation(null, TX_PARAMS, { provider: 'openai' }),
            /result is required/
        );

        await assert.rejects(
            () => explainSimulation(WETH_DEPOSIT_RESULT, { to: '' }, { provider: 'openai' }),
            /txParams.to is required/
        );

        await assert.rejects(
            () => explainSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, {}),
            /config.provider is required/
        );
    });
});

describe('enhanceSimulation', () => {
    it('returns enhanced result with explanation and decoded data', async () => {
        const mockExplanation = 'Deposits 0.1 ETH into WETH.';

        const originalFetch = globalThis.fetch;
        globalThis.fetch = async (url, opts) => {
            if (url.includes('sourcify')) {
                const match = url.match(/\/v2\/contract\/\d+\/([^?]+)/);
                const addr = match?.[1]?.toLowerCase();
                const abi = addr === '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2' ? WETH_ABI : null;
                return new Response(JSON.stringify({
                    abi, sources: null, compilation: null, stdJsonInput: null,
                }), { status: 200, headers: { 'Content-Type': 'application/json' } });
            }
            return new Response(JSON.stringify({
                choices: [{ message: { content: mockExplanation } }],
            }), { status: 200, headers: { 'Content-Type': 'application/json' } });
        };

        try {
            const enhanced = await enhanceSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, {
                provider: 'openai',
                apiKey: 'test-key',
                model: 'gpt-4o-mini',
                chainId: 1,
            });

            assert.equal(enhanced.explanation, mockExplanation);
            assert.equal(enhanced.gasUsed, WETH_DEPOSIT_RESULT.gasUsed);
            assert.ok(enhanced.decodedCall);
            assert.equal(enhanced.decodedCall.name, 'deposit');
            assert.ok(enhanced.trace[0].decoded);
            assert.ok(enhanced.stateChanges[0].storage[0].resolved);

            const json = JSON.stringify(enhanced);
            assert.ok(json, 'must be JSON-serializable');
        } finally {
            globalThis.fetch = originalFetch;
        }
    });

    it('works without chainId (no enrichment)', async () => {
        const originalFetch = globalThis.fetch;
        globalThis.fetch = async () => new Response(JSON.stringify({
            choices: [{ message: { content: 'Simple explanation' } }],
        }), { status: 200, headers: { 'Content-Type': 'application/json' } });

        try {
            const enhanced = await enhanceSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, {
                provider: 'openai',
                apiKey: 'test-key',
            });

            assert.equal(enhanced.explanation, 'Simple explanation');
            assert.equal(enhanced.decodedCall, undefined);
            assert.equal(enhanced.logs.length, 2);
        } finally {
            globalThis.fetch = originalFetch;
        }
    });

    it('validates required inputs', async () => {
        await assert.rejects(
            () => enhanceSimulation(null, TX_PARAMS, { provider: 'openai' }),
            /result is required/
        );
        await assert.rejects(
            () => enhanceSimulation(WETH_DEPOSIT_RESULT, { to: '' }, { provider: 'openai' }),
            /txParams.to is required/
        );
        await assert.rejects(
            () => enhanceSimulation(WETH_DEPOSIT_RESULT, TX_PARAMS, {}),
            /config.provider is required/
        );
    });
});

describe('buildPrompt (re-exported)', () => {
    it('is accessible from the main entry point', () => {
        const { systemPrompt, userPrompt } = buildPrompt(WETH_DEPOSIT_RESULT, TX_PARAMS, {});

        assert.ok(systemPrompt.length > 0);
        assert.ok(userPrompt.length > 0);
    });
});
