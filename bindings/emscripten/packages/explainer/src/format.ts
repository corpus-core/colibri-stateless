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

/**
 * Parse a hex string (with or without `0x` prefix) into a BigInt.
 * Returns 0n for empty, null, array dumps, or any other non-scalar input.
 * Negative quantities (`0x-31380`) are accepted.
 *
 * @param hex - Hex quantity, optional `0x` / `0X`
 * @return Parsed value, or `0n` for empty / non-scalar input
 */
export function hexToBigInt(hex: string | undefined | null): bigint {
    if (hex == null || hex === '' || hex === '0x' || hex === '0X' || hex === '0x0' || hex === '0X0') return 0n;
    const raw = String(hex).trim();
    if (!raw || raw === '0' || raw.startsWith('[')) return 0n;
    try {
        if (/^0x-/i.test(raw)) return -BigInt('0x' + raw.slice(3));
        if (raw.startsWith('-')) return BigInt(raw);
        const clean = raw.startsWith('0x') || raw.startsWith('0X') ? raw : '0x' + raw;
        return BigInt(clean);
    } catch {
        return 0n;
    }
}

/** Format a wei value as a decimal ETH string with up to `precision` decimal places. */
export function weiToEth(weiHex: string, precision = 6): string {
    const wei = hexToBigInt(weiHex);
    if (wei === 0n) return '0';

    const ETH = 10n ** 18n;
    const whole = wei / ETH;
    const remainder = wei % ETH;

    if (remainder === 0n) return whole.toString();

    const factor = 10n ** BigInt(precision);
    const fractional = (remainder * factor) / ETH;
    const fracStr = fractional.toString().padStart(precision, '0').replace(/0+$/, '');

    return fracStr ? `${whole}.${fracStr}` : whole.toString();
}

/**
 * Format a raw token amount given its decimals.
 * Falls back to raw hex if decimals are unknown.
 */
export function formatTokenAmount(rawHex: string, decimals: number): string {
    const raw = hexToBigInt(rawHex);
    if (raw === 0n) return '0';

    const divisor = 10n ** BigInt(decimals);
    const whole = raw / divisor;
    const remainder = raw % divisor;

    if (remainder === 0n) return whole.toString();

    const maxFrac = Math.min(decimals, 8);
    const factor = 10n ** BigInt(maxFrac);
    const fractional = (remainder * factor) / divisor;
    const fracStr = fractional.toString().padStart(maxFrac, '0').replace(/0+$/, '');

    return fracStr ? `${whole}.${fracStr}` : whole.toString();
}

/** Format a hex gas value as a decimal number with thousand separators. */
export function formatGas(gasHex: string): string {
    const gas = Number(hexToBigInt(gasHex));
    return gas.toLocaleString('en-US');
}

/** Shorten an Ethereum address to `0xAbCd...1234` form. */
export function shortenAddress(address: string): string {
    if (!address || address.length < 10) return address || '';
    return address.slice(0, 6) + '...' + address.slice(-4);
}

const KNOWN_SELECTORS: Record<string, string> = {
    '0xd0e30db0': 'deposit()',
    '0xa9059cbb': 'transfer(address,uint256)',
    '0x095ea7b3': 'approve(address,uint256)',
    '0x23b872dd': 'transferFrom(address,address,uint256)',
    '0x70a08231': 'balanceOf(address)',
    '0x18160ddd': 'totalSupply()',
    '0x2e1a7d4d': 'withdraw(uint256)',
    '0x38ed1739': 'swapExactTokensForTokens(...)',
    '0x7ff36ab5': 'swapExactETHForTokens(...)',
    '0x3593564c': 'execute(bytes,bytes[],uint256)',
};

/** Extract the 4-byte function selector from calldata. Returns a known name if recognized. */
export function formatSelector(data: string | undefined): string {
    if (!data || data.length < 10) return '';
    const selector = data.slice(0, 10).toLowerCase();
    return KNOWN_SELECTORS[selector] || selector;
}

/**
 * Return calldata size in bytes, e.g. `"56 bytes"`. Used when the target has
 * no ABI so the model receives a factual size instead of an invented selector
 * (issue #382: EIP-7002 predeploy input is a 48-byte BLS pubkey, not a call).
 *
 * @param data - Hex-encoded calldata (`0x` prefix optional)
 * @return `"<n> bytes"`, or `"0 bytes"` for empty input
 */
export function formatCalldataSize(data: string | undefined | null): string {
    if (!data) return '0 bytes';
    const raw = String(data);
    const hex = raw.startsWith('0x') || raw.startsWith('0X') ? raw.slice(2) : raw;
    const len = Math.floor(hex.length / 2);
    return `${len} bytes`;
}
