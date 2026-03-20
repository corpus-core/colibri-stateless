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

import type { SimulationResult, SimulationLog, StateChange, BalanceChange, AssetChange, TraceEntry, TxParams, PromptConfig } from './types.js';
import { hexToBigInt, weiToEth, formatGas, shortenAddress, formatSelector } from './format.js';
import { labelAddress } from './known_addresses.js';

const BASE_SYSTEM_PROMPT = `You are a blockchain transaction analyst. Your job is to explain \
what an Ethereum transaction would do in clear, simple terms that a non-technical user can understand.

Rules:
- Be concise (2-5 sentences for simple transactions, more for complex ones).
- Mention concrete token amounts and addresses when available.
- If the transaction reverts, clearly state that and explain why if possible.
- Highlight any potential risks (e.g. unlimited approvals, interactions with unverified contracts).
- Do not speculate about information not present in the metadata.
- Do not include raw hex values unless no decoded form is available.`;

export interface PromptParts {
    systemPrompt: string;
    userPrompt: string;
}

/** Build system and user prompts from simulation result and transaction parameters. */
export function buildPrompt(result: SimulationResult, txParams: TxParams, config: PromptConfig): PromptParts {
    const systemPrompt = buildSystemPrompt(config);
    const userPrompt = buildUserPrompt(result, txParams);
    return { systemPrompt, userPrompt };
}

function buildSystemPrompt(config: PromptConfig): string {
    let prompt = BASE_SYSTEM_PROMPT;

    if (config.language && config.language !== 'en') {
        prompt += `\n\nIMPORTANT: Respond in ${languageName(config.language)}.`;
    }

    if (config.systemPromptInclude) {
        prompt += `\n\nAdditional context from the application:\n${config.systemPromptInclude}`;
    }

    return prompt;
}

function buildUserPrompt(result: SimulationResult, txParams: TxParams): string {
    const sections: string[] = [];

    sections.push(formatTxOverview(result, txParams));

    if (result.logs && result.logs.length > 0) {
        sections.push(formatEvents(result.logs));
    }

    if (result.stateChanges && result.stateChanges.length > 0) {
        sections.push(formatStateChanges(result.stateChanges));
    }

    if (result.balanceChanges && result.balanceChanges.length > 0) {
        sections.push(formatBalanceChanges(result.balanceChanges));
    }

    if (result.assetChanges && result.assetChanges.length > 0) {
        sections.push(formatAssetChanges(result.assetChanges));
    }

    if (result.trace && result.trace.length > 0) {
        sections.push(formatTrace(result.trace));
    }

    sections.push('Please explain what this transaction would do.');

    return sections.join('\n\n');
}

function formatTxOverview(result: SimulationResult, txParams: TxParams): string {
    const status = result.status === '0x1' ? 'SUCCESS' : 'REVERTED';
    const to = labelAddress(txParams.to, shortenAddress);
    const from = txParams.from ? labelAddress(txParams.from, shortenAddress) : 'unknown sender';
    const value = txParams.value ? weiToEth(txParams.value) : '0';
    const selector = formatSelector(txParams.data);
    const gas = formatGas(result.gasUsed);

    let overview = `## Transaction Overview\n`;
    overview += `- Status: ${status}\n`;
    overview += `- From: ${from}\n`;
    overview += `- To: ${to}\n`;
    if (value !== '0') overview += `- Value: ${value} ETH\n`;
    if (selector) overview += `- Function selector: ${selector}\n`;
    overview += `- Gas used: ${gas}`;

    return overview;
}

function formatEvents(logs: SimulationLog[]): string {
    const lines: string[] = ['## Emitted Events'];

    for (let i = 0; i < logs.length; i++) {
        const log = logs[i];
        const contractAddr = log.raw?.address
            ? labelAddress(log.raw.address, shortenAddress)
            : 'unknown contract';

        if (log.name && log.inputs) {
            const params = log.inputs.map(p => formatEventParam(p)).join(', ');
            lines.push(`${i + 1}. **${log.name}** on ${contractAddr}`);
            lines.push(`   Parameters: ${params}`);
        } else {
            const topic0 = log.raw?.topics?.[0];
            lines.push(`${i + 1}. Unknown event on ${contractAddr}`);
            if (topic0) lines.push(`   Topic: ${shortenAddress(topic0)}`);
        }
    }

    return lines.join('\n');
}

function formatEventParam(param: { name: string; type: string; value: string }): string {
    if (param.type === 'address') {
        return `${param.name}=${labelAddress(param.value, shortenAddress)}`;
    }
    if (param.type.startsWith('uint') || param.type.startsWith('int')) {
        return `${param.name}=${formatNumericParam(param.value, param.name)}`;
    }
    return `${param.name}=${param.value}`;
}

const AMOUNT_PARAM_NAMES = new Set([
    'value', 'amount', 'wad', 'amount0', 'amount1',
    'amount0In', 'amount1In', 'amount0Out', 'amount1Out',
]);

/**
 * Heuristic: if the parameter name suggests a token amount and the value
 * looks large (>= 10^14), format it as ETH-scale (18 decimals).
 */
function formatNumericParam(hexValue: string, name: string): string {
    const val = hexToBigInt(hexValue);
    if (AMOUNT_PARAM_NAMES.has(name) && val >= 10n ** 14n) {
        return `${weiToEth(hexValue)} (raw: ${val.toString()})`;
    }
    return val.toString();
}

function formatStateChanges(changes: StateChange[]): string {
    const lines: string[] = ['## State Changes'];
    for (const c of changes) {
        const addr = labelAddress(c.address, shortenAddress);
        const varName = c.soltype ? `${c.soltype.name} (${c.soltype.type})` : shortenAddress(c.slot);
        lines.push(`- ${addr}: ${varName}: ${shortenAddress(c.oldValue)} -> ${shortenAddress(c.newValue)}`);
    }
    return lines.join('\n');
}

function formatBalanceChanges(changes: BalanceChange[]): string {
    const lines: string[] = ['## Balance Changes'];
    for (const c of changes) {
        const addr = labelAddress(c.address, shortenAddress);
        const oldBal = weiToEth(c.oldBalance);
        const newBal = weiToEth(c.newBalance);
        lines.push(`- ${addr}: ${oldBal} ETH -> ${newBal} ETH`);
    }
    return lines.join('\n');
}

function formatAssetChanges(changes: AssetChange[]): string {
    const lines: string[] = ['## Asset Changes'];
    for (const c of changes) {
        const from = labelAddress(c.from, shortenAddress);
        const to = labelAddress(c.to, shortenAddress);
        const symbol = c.tokenInfo?.symbol || '???';
        lines.push(`- ${c.type}: ${c.amount} ${symbol} from ${from} to ${to}`);
    }
    return lines.join('\n');
}

function formatTrace(trace: TraceEntry[]): string {
    const lines: string[] = ['## Call Trace'];
    for (let i = 0; i < Math.min(trace.length, 20); i++) {
        const t = trace[i];
        const from = t.from ? labelAddress(t.from, shortenAddress) : '?';
        const to = t.to ? labelAddress(t.to, shortenAddress) : '?';
        const method = t.method || t.type || 'CALL';
        const value = t.value && t.value !== '0x0' ? ` (${weiToEth(t.value)} ETH)` : '';
        lines.push(`${i + 1}. ${from} -> ${to}: ${method}${value}`);
    }
    if (trace.length > 20) {
        lines.push(`... and ${trace.length - 20} more calls`);
    }
    return lines.join('\n');
}

function languageName(code: string): string {
    const names: Record<string, string> = {
        de: 'German', es: 'Spanish', fr: 'French', it: 'Italian',
        pt: 'Portuguese', nl: 'Dutch', pl: 'Polish', ja: 'Japanese',
        ko: 'Korean', zh: 'Chinese', ru: 'Russian', ar: 'Arabic',
        tr: 'Turkish', uk: 'Ukrainian', sv: 'Swedish', da: 'Danish',
        fi: 'Finnish', no: 'Norwegian', cs: 'Czech', ro: 'Romanian',
        hu: 'Hungarian', el: 'Greek', th: 'Thai', vi: 'Vietnamese',
    };
    return names[code] || code;
}
