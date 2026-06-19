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

import type {
    SimulationResult, SimulationLog, ContractStateChange, TraceEntry,
    TxParams, PromptConfig, EnrichedContext, ResolvedSlot, ContractMetadata,
} from './types.js';
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
export function buildPrompt(
    result: SimulationResult,
    txParams: TxParams,
    config: PromptConfig,
    context?: EnrichedContext,
): PromptParts {
    const systemPrompt = buildSystemPrompt(config);
    const userPrompt = buildUserPrompt(result, txParams, context, config.maxSourceChars);
    return { systemPrompt, userPrompt };
}

/** Default source-code character budget embedded into the prompt. */
const DEFAULT_MAX_SOURCE_CHARS = 10000;

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

function buildUserPrompt(result: SimulationResult, txParams: TxParams, context?: EnrichedContext, maxSourceChars?: number): string {
    const sections: string[] = [];

    sections.push(formatTxOverview(result, txParams, context));

    if (result.logs && result.logs.length > 0) {
        sections.push(formatEvents(result.logs, context));
    }

    if (result.stateChanges && result.stateChanges.length > 0) {
        sections.push(formatStateChanges(result.stateChanges, context));
    }

    if (result.trace && result.trace.length > 0) {
        sections.push(formatTrace(result.trace, context));
    }

    if (context) {
        const sourceSection = formatSourceContext(result, context, maxSourceChars);
        if (sourceSection) sections.push(sourceSection);
    }

    sections.push('Please explain what this transaction would do.');

    return sections.join('\n\n');
}

function formatTxOverview(result: SimulationResult, txParams: TxParams, context?: EnrichedContext): string {
    const status = result.status === '0x1' ? 'SUCCESS' : 'REVERTED';
    const to = labelAddress(txParams.to, shortenAddress);
    const from = txParams.from ? labelAddress(txParams.from, shortenAddress) : 'unknown sender';
    const value = txParams.value ? weiToEth(txParams.value) : '0';
    const gas = formatGas(result.gasUsed);

    let overview = `## Transaction Overview\n`;
    overview += `- Status: ${status}\n`;
    overview += `- From: ${from}\n`;
    overview += `- To: ${to}\n`;
    if (value !== '0') overview += `- Value: ${value} ETH\n`;

    if (context?.decodedCall) {
        const dc = context.decodedCall;
        const params = dc.params.map(p => `${p.name}=${p.value}`).join(', ');
        overview += `- Function: ${dc.name}(${params})\n`;
    } else {
        const selector = formatSelector(txParams.data);
        if (selector) overview += `- Function selector: ${selector}\n`;
    }

    overview += `- Gas used: ${gas}\n`;

    if (result.status !== '0x1' && context?.decodedError) {
        const err = context.decodedError;
        if (err.reason) {
            overview += `- Revert reason: ${err.reason}\n`;
        } else {
            const params = err.params.map(p => `${p.name}=${p.value}`).join(', ');
            overview += `- Revert error: ${err.name}(${params})\n`;
        }
    }

    return overview.trimEnd();
}

function formatEvents(logs: SimulationLog[], context?: EnrichedContext): string {
    const lines: string[] = ['## Emitted Events'];

    for (let i = 0; i < logs.length; i++) {
        const log = logs[i];
        const contractAddr = log.raw?.address
            ? labelAddress(log.raw.address, shortenAddress)
            : 'unknown contract';

        const decoded = context?.decodedEvents?.[i];

        if (decoded) {
            const params = decoded.params.map(p => formatEventParam(p)).join(', ');
            lines.push(`${i + 1}. **${decoded.name}** on ${contractAddr}`);
            lines.push(`   Parameters: ${params}`);
        } else if (log.name && log.inputs) {
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

function formatStateChanges(changes: ContractStateChange[], context?: EnrichedContext): string {
    const lines: string[] = ['## State Changes'];

    for (const change of changes) {
        const addr = labelAddress(change.address, shortenAddress);
        const resolvedSlots = context?.resolvedStorage?.get(change.address.toLowerCase());

        if (change.balance) {
            const oldBal = weiToEth(change.balance.previousValue);
            const newBal = weiToEth(change.balance.newValue);
            lines.push(`- ${addr}: balance ${oldBal} ETH -> ${newBal} ETH`);
        }

        if (change.storage) {
            for (let i = 0; i < change.storage.length; i++) {
                const s = change.storage[i];
                const resolved = resolvedSlots?.[i];

                if (resolved?.variableName) {
                    const varLabel = formatResolvedLabel(resolved);
                    const typeHint = resolved.variableType ? ` (${resolved.variableType})` : '';
                    lines.push(`- ${addr}: ${varLabel}${typeHint}: ${formatSlotValue(s.previousValue)} -> ${formatSlotValue(s.newValue)}`);
                } else if (resolved && typeof resolved.baseSlot === 'number' && resolved.baseSlot >= 0) {
                    const keyStr = resolved.keys?.map(k => `[${formatKeyValue(k)}]`).join('') || '';
                    lines.push(`- ${addr}: slot ${resolved.baseSlot}${keyStr}: ${formatSlotValue(s.previousValue)} -> ${formatSlotValue(s.newValue)}`);
                } else {
                    lines.push(`- ${addr}: ${shortenAddress(s.slot)}: ${formatSlotValue(s.previousValue)} -> ${formatSlotValue(s.newValue)}`);
                }
            }
        }
    }

    return lines.join('\n');
}

function formatResolvedLabel(resolved: ResolvedSlot): string {
    let label = resolved.variableName || `slot ${resolved.baseSlot}`;
    if (resolved.keys?.length) {
        label += resolved.keys.map(k => `[${formatKeyValue(k)}]`).join('');
    }
    if (resolved.arrayIndex !== undefined) {
        label += `[${resolved.arrayIndex}]`;
    }
    if (resolved.structField) {
        label += `.${resolved.structField}`;
    }
    return label;
}

function formatKeyValue(key: { type: string; value: string }): string {
    if (key.type === 'address') return labelAddress(key.value, shortenAddress);
    return key.value;
}

function formatSlotValue(hex: string): string {
    const val = hexToBigInt(hex);
    if (val === 0n) return '0';
    return val.toString();
}

function formatTrace(trace: TraceEntry[], context?: EnrichedContext): string {
    const lines: string[] = ['## Call Trace'];
    const limit = Math.min(trace.length, 20);

    for (let i = 0; i < limit; i++) {
        const t = trace[i];
        const from = t.from ? labelAddress(t.from, shortenAddress) : '?';
        const to = t.to ? labelAddress(t.to, shortenAddress) : '?';
        const callType = t.type || 'CALL';
        const value = t.value && t.value !== '0x0' && t.value !== '0x' ? ` (${weiToEth(t.value)} ETH)` : '';

        const decoded = context?.decodedTrace?.[i];
        if (decoded) {
            const params = decoded.params.map(p => `${p.name}=${p.value}`).join(', ');
            lines.push(`${i + 1}. ${from} -> ${to}: ${decoded.name}(${params})${value} [${callType}]`);
        } else {
            const selector = t.input ? formatSelector(t.input) : '';
            const method = selector || callType;
            lines.push(`${i + 1}. ${from} -> ${to}: ${method}${value}`);
        }
    }

    if (trace.length > 20) {
        lines.push(`... and ${trace.length - 20} more calls`);
    }

    return lines.join('\n');
}

/**
 * Include source code context for contracts where storage layout is unavailable,
 * so the LLM can reason about storage variable assignments.
 */
function formatSourceContext(result: SimulationResult, context: EnrichedContext, maxSourceChars?: number): string | null {
    const contractsNeedingSource = new Set<string>();

    if (result.stateChanges) {
        for (const change of result.stateChanges) {
            const addr = change.address.toLowerCase();
            const resolved = context.resolvedStorage?.get(addr);
            const meta = context.contracts?.get(addr);

            const hasUnresolvedSlots = change.storage?.some((_, i) => resolved?.[i] && !resolved[i].variableName);
            if (hasUnresolvedSlots && meta?.sources) {
                contractsNeedingSource.add(addr);
            }
        }
    }

    if (contractsNeedingSource.size === 0) return null;

    const lines: string[] = ['## Contract Source Code (for storage interpretation)'];
    // Total character budget for embedded source code. Lower this for local
    // models with a small context window via `config.maxSourceChars`.
    let totalBudget = maxSourceChars && maxSourceChars > 0 ? maxSourceChars : DEFAULT_MAX_SOURCE_CHARS;
    // Per-file cap stays proportional to the overall budget so a single large
    // file cannot consume the entire context.
    const perFileCap = Math.max(500, Math.floor(totalBudget * 0.3));

    for (const addr of contractsNeedingSource) {
        if (totalBudget <= 0) break;
        const meta = context.contracts.get(addr)!;
        const label = labelAddress(addr, shortenAddress);
        lines.push(`\n### ${label}`);

        if (meta.sources) {
            for (const [filename, source] of Object.entries(meta.sources)) {
                if (totalBudget <= 0) break;
                const content = source.content;
                const maxLen = Math.min(content.length, totalBudget, perFileCap);
                const truncated = content.length > maxLen ? content.slice(0, maxLen) + '\n... (truncated)' : content;
                totalBudget -= truncated.length;
                lines.push(`\`${filename}\`:\n\`\`\`solidity\n${truncated}\n\`\`\``);
            }
        }
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
