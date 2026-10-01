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

export interface KnownAddress {
    label: string;
    decimals?: number;
    symbol?: string;
    /**
     * Short trusted description injected as a `NOTE` into the prompt. Used for
     * hand-written EVM predeploys (EIP-4788, EIP-2935, EIP-7002, EIP-7251)
     * where the model would otherwise invent a selector or misread raw storage
     * (issue #382).
     */
    description?: string;
    /**
     * If true, the address hosts no ABI-based Solidity contract. Suppresses
     * ABI/selector guesses in the prompt so hand-written EVM inputs (e.g. a
     * 48-byte BLS pubkey for EIP-7002) are not labelled as function calls.
     */
    noAbi?: boolean;
}

const KNOWN: Record<string, KnownAddress> = {
    '0x0000000000000000000000000000000000000000': { label: 'Null Address' },
    '0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2': { label: 'WETH', decimals: 18, symbol: 'WETH' },
    '0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48': { label: 'USDC', decimals: 6, symbol: 'USDC' },
    '0xdac17f958d2ee523a2206206994597c13d831ec7': { label: 'USDT', decimals: 6, symbol: 'USDT' },
    '0x6b175474e89094c44da98b954eedeac495271d0f': { label: 'DAI', decimals: 18, symbol: 'DAI' },
    '0x2260fac5e5542a773aa44fbcfedf7c193bc2c599': { label: 'WBTC', decimals: 8, symbol: 'WBTC' },
    '0x7a250d5630b4cf539739df2c5dacb4c659f2488d': { label: 'Uniswap V2 Router' },
    '0xe592427a0aece92de3edee1f18e0157c05861564': { label: 'Uniswap V3 Router' },
    '0x68b3465833fb72a70ecdf485e0e4c7bd8665fc45': { label: 'Uniswap SwapRouter02' },
    '0x3fc91a3afd70395cd496c647d5a6cc9d4b2b7fad': { label: 'Uniswap Universal Router' },
    '0x7f268357a8c2552623316e2562d90e642bb538e5': { label: 'OpenSea Wyvern' },
    '0x00000000006c3852cbef3e08e8df289169ede581': { label: 'Seaport 1.1' },
    '0x1111111254eeb25477b68fb85ed929f73a960582': { label: '1inch Router' },
    '0xdef1c0ded9bec7f1a1670819833240f027b25eff': { label: '0x Exchange Proxy' },
    // -- System / consensus-layer predeploys (no ABI, hand-written EVM). --
    '0x000f3df6d732807ef1319fb7b8bb8522d0beac02': {
        label: 'EIP-4788 Beacon Root',
        description: 'EIP-4788 predeploy. `set` (called by the SYSTEM_ADDRESS at slot start) writes the current beacon block root to a 8191-slot ring buffer; any caller reads a stored root by passing a `uint64` timestamp as calldata and receiving the 32-byte root. No ABI, no Solidity source.',
        noAbi: true,
    },
    '0x0000f90827f1c53a10cb7a02335b175320002935': {
        label: 'EIP-2935 Historical Block Hashes',
        description: 'EIP-2935 predeploy. `set` (called by the SYSTEM_ADDRESS at slot start) writes `blockhash(number-1)` to a 8191-slot ring buffer; any caller queries a historical hash by passing the block number as 32-byte calldata and receiving the 32-byte hash. No ABI, no Solidity source.',
        noAbi: true,
    },
    '0x00000961ef480eb55e80d19ad83579a64c007002': {
        label: 'EIP-7002 Execution-Layer Withdrawal Request',
        description: 'EIP-7002 predeploy. A validator posts a withdrawal request by calling with 56 bytes: `pubkey (48) || amount (8)` and the required fee in `msg.value`. `set` (called by the SYSTEM_ADDRESS at slot start) drains the queue for the block. No ABI, no Solidity source.',
        noAbi: true,
    },
    '0x0000bbddc7ce488642fb579f8b00f3a590007251': {
        label: 'EIP-7251 Execution-Layer Consolidation Request',
        description: 'EIP-7251 predeploy. A validator posts a consolidation request by calling with 96 bytes: `source_pubkey (48) || target_pubkey (48)` and the required fee in `msg.value`. `set` (called by the SYSTEM_ADDRESS at slot start) drains the queue for the block. No ABI, no Solidity source.',
        noAbi: true,
    },
};

/** Look up a known address label. Returns `undefined` if unknown. */
export function lookupAddress(address: string): KnownAddress | undefined {
    return KNOWN[address.toLowerCase()];
}

/**
 * Format an address with its label if known.
 *
 * @returns e.g. `"WETH (0xC02a...6Cc2)"` or `"0xC02a...6Cc2"` if unknown.
 */
export function labelAddress(address: string, shorten: (a: string) => string): string {
    const known = lookupAddress(address);
    const short = shorten(address);
    return known ? `${known.label} (${short})` : short;
}
