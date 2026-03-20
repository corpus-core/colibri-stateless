import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { buildPrompt } from '../dist/prompt.js';
import { WETH_DEPOSIT_RESULT, TX_PARAMS } from './fixtures.mjs';

describe('buildPrompt', () => {
    it('produces system and user prompts', () => {
        const { systemPrompt, userPrompt } = buildPrompt(WETH_DEPOSIT_RESULT, TX_PARAMS, {});

        assert.ok(systemPrompt.includes('blockchain transaction analyst'));
        assert.ok(userPrompt.includes('Transaction Overview'));
    });

    it('includes known address labels in user prompt', () => {
        const { userPrompt } = buildPrompt(WETH_DEPOSIT_RESULT, TX_PARAMS, {});
        assert.ok(userPrompt.includes('WETH'), `Expected WETH label, got:\n${userPrompt}`);
    });

    it('formats ETH value in overview', () => {
        const { userPrompt } = buildPrompt(WETH_DEPOSIT_RESULT, TX_PARAMS, {});
        assert.ok(userPrompt.includes('0.1 ETH'), `Expected 0.1 ETH, got:\n${userPrompt}`);
    });

    it('lists decoded events', () => {
        const { userPrompt } = buildPrompt(WETH_DEPOSIT_RESULT, TX_PARAMS, {});
        assert.ok(userPrompt.includes('**Transfer**'), `Expected Transfer event, got:\n${userPrompt}`);
        assert.ok(userPrompt.includes('**Deposit**'), `Expected Deposit event, got:\n${userPrompt}`);
    });

    it('formats gas used', () => {
        const { userPrompt } = buildPrompt(WETH_DEPOSIT_RESULT, TX_PARAMS, {});
        assert.ok(userPrompt.includes('45,038'), `Expected formatted gas, got:\n${userPrompt}`);
    });

    it('resolves known function selectors', () => {
        const { userPrompt } = buildPrompt(WETH_DEPOSIT_RESULT, TX_PARAMS, {});
        assert.ok(userPrompt.includes('deposit()'), `Expected deposit() selector, got:\n${userPrompt}`);
    });

    it('appends language instruction to system prompt', () => {
        const { systemPrompt } = buildPrompt(WETH_DEPOSIT_RESULT, TX_PARAMS, { language: 'de' });
        assert.ok(systemPrompt.includes('German'), `Expected German instruction, got:\n${systemPrompt}`);
    });

    it('appends systemPromptInclude to system prompt', () => {
        const include = 'This is a DeFi wallet. Focus on risks.';
        const { systemPrompt } = buildPrompt(WETH_DEPOSIT_RESULT, TX_PARAMS, { systemPromptInclude: include });
        assert.ok(systemPrompt.includes(include), `Expected include text, got:\n${systemPrompt}`);
    });

    it('handles a reverted transaction', () => {
        const revertedResult = { gasUsed: '0x5208', status: '0x0', returnValue: '0x', logs: [] };
        const { userPrompt } = buildPrompt(revertedResult, TX_PARAMS, {});
        assert.ok(userPrompt.includes('REVERTED'), `Expected REVERTED status, got:\n${userPrompt}`);
    });

    it('handles unknown events gracefully', () => {
        const unknownEventResult = {
            gasUsed: '0x100',
            status: '0x1',
            returnValue: '0x',
            logs: [{
                raw: {
                    address: '0x1234567890abcdef1234567890abcdef12345678',
                    data: '0x',
                    topics: ['0xabcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890'],
                },
            }],
        };
        const { userPrompt } = buildPrompt(unknownEventResult, TX_PARAMS, {});
        assert.ok(userPrompt.includes('Unknown event'), `Expected unknown event, got:\n${userPrompt}`);
    });
});
