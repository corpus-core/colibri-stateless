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

import { Interface, AbiCoder, type InterfaceAbi, type Result, type LogDescription } from 'ethers';
import type { DecodedCall, DecodedEvent, DecodedError } from './types.js';

const PANIC_REASONS: Record<number, string> = {
    0x00: 'Generic compiler panic',
    0x01: 'Assert condition failed',
    0x11: 'Arithmetic overflow or underflow',
    0x12: 'Division or modulo by zero',
    0x21: 'Conversion to invalid enum value',
    0x22: 'Access to incorrectly encoded storage byte array',
    0x31: 'Pop on empty array',
    0x32: 'Array index out of bounds',
    0x41: 'Too much memory allocated',
    0x51: 'Call to zero-initialized function variable',
};

/**
 * Decode a function call's calldata using the contract ABI.
 * Returns null if the selector is not found in the ABI.
 */
export function decodeFunctionCall(abi: unknown[], data: string): DecodedCall | null {
    if (!data || data.length < 10) return null;
    try {
        const iface = new Interface(abi as InterfaceAbi);
        const parsed = iface.parseTransaction({ data });
        if (!parsed) return null;

        return {
            name: parsed.name,
            signature: parsed.signature,
            params: formatParams(parsed.fragment.inputs, parsed.args),
        };
    } catch {
        return null;
    }
}

/**
 * Decode an event log using the contract ABI.
 * Returns null if the topic is not recognized by the ABI.
 */
export function decodeEventLog(
    abi: unknown[],
    log: { topics: string[]; data: string },
): DecodedEvent | null {
    if (!log.topics || log.topics.length === 0) return null;
    try {
        const iface = new Interface(abi as InterfaceAbi);
        const parsed: LogDescription | null = iface.parseLog({
            topics: log.topics,
            data: log.data,
        });
        if (!parsed) return null;

        return {
            name: parsed.name,
            signature: parsed.signature,
            params: parsed.fragment.inputs.map((input, i) => ({
                name: input.name || `param${i}`,
                type: input.type,
                value: formatValue(parsed.args[i]),
                indexed: input.indexed ?? false,
            })),
        };
    } catch {
        return null;
    }
}

/**
 * Decode a function's return data using the contract ABI.
 * Requires the calldata (or at least selector) to identify the function.
 */
export function decodeFunctionResult(
    abi: unknown[],
    calldata: string,
    resultData: string,
): DecodedCall | null {
    if (!calldata || calldata.length < 10 || !resultData || resultData === '0x') return null;
    try {
        const iface = new Interface(abi as InterfaceAbi);
        const parsed = iface.parseTransaction({ data: calldata });
        if (!parsed) return null;

        const outputs = parsed.fragment.outputs;
        if (!outputs || outputs.length === 0) return null;

        const decoded = iface.decodeFunctionResult(parsed.fragment, resultData);
        return {
            name: parsed.name,
            signature: parsed.signature,
            params: formatParams(outputs, decoded),
        };
    } catch {
        return null;
    }
}

/**
 * Decode revert data from a failed transaction.
 *
 * Handles three cases in order:
 * 1. `Error(string)` -- standard `revert("reason")` (selector `0x08c379a0`)
 * 2. `Panic(uint256)` -- `assert` failures (selector `0x4e487b71`)
 * 3. Custom errors defined in the contract ABI
 *
 * @param data - The hex-encoded revert data (returnValue from a failed tx)
 * @param abi - Optional contract ABI for custom error decoding
 * @return Decoded error or null if data cannot be decoded
 */
export function decodeRevertData(data: string, abi?: unknown[]): DecodedError | null {
    if (!data || data === '0x' || data.length < 10) return null;

    const selector = data.slice(0, 10).toLowerCase();

    if (selector === '0x08c379a0') {
        try {
            const coder = AbiCoder.defaultAbiCoder();
            const [reason] = coder.decode(['string'], '0x' + data.slice(10));
            return {
                name: 'Error',
                signature: 'Error(string)',
                params: [{ name: 'reason', type: 'string', value: reason }],
                reason,
            };
        } catch { return null; }
    }

    if (selector === '0x4e487b71') {
        try {
            const coder = AbiCoder.defaultAbiCoder();
            const [code] = coder.decode(['uint256'], '0x' + data.slice(10));
            const codeNum = Number(code);
            const reason = PANIC_REASONS[codeNum] || `Panic code 0x${codeNum.toString(16)}`;
            return {
                name: 'Panic',
                signature: 'Panic(uint256)',
                params: [{ name: 'code', type: 'uint256', value: '0x' + codeNum.toString(16) }],
                reason,
            };
        } catch { return null; }
    }

    if (abi && abi.length > 0) {
        try {
            const iface = new Interface(abi as InterfaceAbi);
            const parsed = iface.parseError(data);
            if (parsed) {
                return {
                    name: parsed.name,
                    signature: parsed.signature,
                    params: formatParams(parsed.fragment.inputs, parsed.args),
                };
            }
        } catch { /* fall through */ }
    }

    return null;
}

function formatParams(
    inputs: readonly { name: string; type: string }[],
    args: Result,
): { name: string; type: string; value: string }[] {
    return inputs.map((input, i) => ({
        name: input.name || `param${i}`,
        type: input.type,
        value: formatValue(args[i]),
    }));
}

function formatValue(val: unknown): string {
    if (val === null || val === undefined) return '';
    if (typeof val === 'bigint') return '0x' + val.toString(16);
    if (typeof val === 'string') return val;
    if (typeof val === 'boolean') return val.toString();
    if (Array.isArray(val)) return `[${val.map(v => formatValue(v)).join(', ')}]`;
    return String(val);
}
