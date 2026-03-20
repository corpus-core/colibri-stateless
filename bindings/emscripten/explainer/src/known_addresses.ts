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
    '0x68b3465833fb72a70ecdf485e0e4c7bd8665fc45': { label: 'Uniswap Universal Router' },
    '0x7f268357a8c2552623316e2562d90e642bb538e5': { label: 'OpenSea Wyvern' },
    '0x00000000006c3852cbef3e08e8df289169ede581': { label: 'Seaport 1.1' },
    '0x1111111254eeb25477b68fb85ed929f73a960582': { label: '1inch Router' },
    '0xdef1c0ded9bec7f1a1670819833240f027b25eff': { label: '0x Exchange Proxy' },
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
