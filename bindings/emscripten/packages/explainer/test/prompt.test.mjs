import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { buildPrompt } from '../dist/prompt.js';
import { WETH_DEPOSIT_RESULT, TX_PARAMS, REVERTED_TX_RESULT } from './fixtures.mjs';

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

    it('includes state changes with balance and storage', () => {
        const { userPrompt } = buildPrompt(WETH_DEPOSIT_RESULT, TX_PARAMS, {});
        assert.ok(userPrompt.includes('State Changes'), `Expected State Changes section, got:\n${userPrompt}`);
        assert.ok(userPrompt.includes('balance'), `Expected balance change, got:\n${userPrompt}`);
    });

    it('includes call trace', () => {
        const { userPrompt } = buildPrompt(WETH_DEPOSIT_RESULT, TX_PARAMS, {});
        assert.ok(userPrompt.includes('Call Trace'), `Expected Call Trace section, got:\n${userPrompt}`);
    });

    it('uses decoded call from enriched context', () => {
        const context = {
            contracts: new Map(),
            decodedCall: { name: 'deposit', signature: 'deposit()', params: [] },
            resolvedStorage: new Map(),
            decodedTrace: [],
            decodedEvents: [],
        };
        const { userPrompt } = buildPrompt(WETH_DEPOSIT_RESULT, TX_PARAMS, {}, context);
        assert.ok(userPrompt.includes('Function: deposit()'), `Expected decoded function, got:\n${userPrompt}`);
    });

    it('uses resolved storage from enriched context', () => {
        const context = {
            contracts: new Map(),
            resolvedStorage: new Map([
                ['0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2', [
                    {
                        variableName: 'balanceOf',
                        variableType: 'mapping(address => uint256)',
                        keys: [{ type: 'address', value: '0x3610bad33aac567d2c5fb03e47eec5c2172fd42a' }],
                        baseSlot: 3,
                        raw: 'test',
                    },
                ]],
            ]),
            decodedTrace: [],
            decodedEvents: [],
        };
        const { userPrompt } = buildPrompt(WETH_DEPOSIT_RESULT, TX_PARAMS, {}, context);
        assert.ok(userPrompt.includes('balanceOf'), `Expected balanceOf variable, got:\n${userPrompt}`);
    });

    it('includes decoded revert reason in prompt for failed tx', () => {
        const context = {
            contracts: new Map(),
            resolvedStorage: new Map(),
            decodedTrace: [],
            decodedEvents: [],
            decodedError: {
                name: 'Error',
                signature: 'Error(string)',
                params: [{ name: 'reason', type: 'string', value: 'Insufficient balance' }],
                reason: 'Insufficient balance',
            },
        };
        const { userPrompt } = buildPrompt(REVERTED_TX_RESULT, TX_PARAMS, {}, context);
        assert.ok(userPrompt.includes('REVERTED'), `Expected REVERTED in prompt`);
        assert.ok(userPrompt.includes('Insufficient balance'), `Expected revert reason in prompt`);
    });

    it('includes custom error name in prompt when no reason string', () => {
        const context = {
            contracts: new Map(),
            resolvedStorage: new Map(),
            decodedTrace: [],
            decodedEvents: [],
            decodedError: {
                name: 'InsufficientBalance',
                signature: 'InsufficientBalance(uint256,uint256)',
                params: [
                    { name: 'available', type: 'uint256', value: '100' },
                    { name: 'required', type: 'uint256', value: '200' },
                ],
            },
        };
        const { userPrompt } = buildPrompt(REVERTED_TX_RESULT, TX_PARAMS, {}, context);
        assert.ok(userPrompt.includes('InsufficientBalance'), `Expected custom error name in prompt`);
        assert.ok(userPrompt.includes('available=100'), `Expected error params in prompt`);
    });
});

describe('buildPrompt source-code budget (maxSourceChars)', () => {
    const WETH_ADDR = '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2';

    // formatSourceContext only embeds source code when a state-changed contract
    // has a storage slot whose resolved entry exists but lacks a variableName,
    // and metadata sources are available.
    function sourceContext(content, fileCount = 1) {
        const sources = {};
        for (let i = 0; i < fileCount; i++) sources[`F${i}.sol`] = { content };
        return {
            contracts: new Map([[WETH_ADDR, { abi: null, storageLayout: null, sources }]]),
            resolvedStorage: new Map([[WETH_ADDR, [{ baseSlot: -1, raw: 'x' }]]]),
            decodedTrace: [],
            decodedEvents: [],
        };
    }

    it('embeds contract source when slots are unresolved', () => {
        const { userPrompt } = buildPrompt(WETH_DEPOSIT_RESULT, TX_PARAMS, {}, sourceContext('contract C {}'));
        assert.ok(userPrompt.includes('## Contract Source Code'));
        assert.ok(userPrompt.includes('contract C {}'));
        assert.ok(!userPrompt.includes('(truncated)'));
    });

    it('truncates a large source file at the default per-file cap', () => {
        const { userPrompt } = buildPrompt(WETH_DEPOSIT_RESULT, TX_PARAMS, {}, sourceContext('A'.repeat(5000)));
        assert.ok(userPrompt.includes('(truncated)'));
    });

    it('a small maxSourceChars budget truncates what the default keeps', () => {
        const med = 'B'.repeat(2000);
        const kept = buildPrompt(WETH_DEPOSIT_RESULT, TX_PARAMS, {}, sourceContext(med)).userPrompt;
        const cut = buildPrompt(WETH_DEPOSIT_RESULT, TX_PARAMS, { maxSourceChars: 600 }, sourceContext(med)).userPrompt;
        assert.ok(!kept.includes('(truncated)'));
        assert.ok(cut.includes('(truncated)'));
    });

    it('falls back to the default budget for non-positive maxSourceChars', () => {
        const med = 'B'.repeat(2000);
        const out = buildPrompt(WETH_DEPOSIT_RESULT, TX_PARAMS, { maxSourceChars: 0 }, sourceContext(med)).userPrompt;
        assert.ok(!out.includes('(truncated)'));
    });

    it('shares the budget across multiple source files', () => {
        const { userPrompt } = buildPrompt(
            WETH_DEPOSIT_RESULT, TX_PARAMS, { maxSourceChars: 600 }, sourceContext('C'.repeat(2000), 3),
        );
        assert.ok(userPrompt.includes('(truncated)'));
        // The third file must be dropped once the budget is exhausted.
        assert.ok(!userPrompt.includes('F2.sol'));
    });

    it('omits source code when storage slots are resolved', () => {
        const ctx = {
            contracts: new Map([[WETH_ADDR, { abi: null, storageLayout: null, sources: { 'F.sol': { content: 'X' } } }]]),
            resolvedStorage: new Map([[WETH_ADDR, [{ variableName: 'balances', baseSlot: 3, raw: 'x' }]]]),
            decodedTrace: [],
            decodedEvents: [],
        };
        const { userPrompt } = buildPrompt(WETH_DEPOSIT_RESULT, TX_PARAMS, {}, ctx);
        assert.ok(!userPrompt.includes('## Contract Source Code'));
    });
});
