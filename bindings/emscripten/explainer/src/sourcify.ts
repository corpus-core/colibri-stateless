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

import type { ContractMetadata, SolidityStorageLayout } from './types.js';

const DEFAULT_BASE_URL = 'https://sourcify.dev/server';
const EMPTY_METADATA: ContractMetadata = { abi: null, sources: null, storageLayout: null };

/**
 * Fetch contract metadata (ABI, source files, storage layout) from the
 * Sourcify v2 API. Returns null fields for unverified contracts.
 *
 * @param address - Contract address (checksummed or lowercase)
 * @param chainId - EVM chain ID (e.g. 1 for Ethereum mainnet)
 * @param baseUrl - Override Sourcify server URL for self-hosted instances
 * @return Contract metadata with nullable fields
 */
export async function fetchContractMetadata(
    address: string,
    chainId: number,
    baseUrl?: string,
): Promise<ContractMetadata> {
    const base = (baseUrl || DEFAULT_BASE_URL).replace(/\/$/, '');
    const url = `${base}/v2/contract/${chainId}/${address}?fields=abi,sources,storageLayout`;

    let response: Response;
    try {
        response = await fetch(url);
    } catch {
        return EMPTY_METADATA;
    }

    if (!response.ok) {
        return EMPTY_METADATA;
    }

    let body: Record<string, unknown>;
    try {
        body = await response.json() as Record<string, unknown>;
    } catch {
        return EMPTY_METADATA;
    }

    const abi = Array.isArray(body.abi) ? body.abi : null;
    const sources = isSourcesObject(body.sources) ? body.sources : null;
    const storageLayout = isStorageLayout(body.storageLayout)
        ? body.storageLayout as SolidityStorageLayout
        : null;

    return { abi, sources, storageLayout };
}

function isSourcesObject(val: unknown): val is Record<string, { content: string }> {
    if (!val || typeof val !== 'object' || Array.isArray(val)) return false;
    for (const entry of Object.values(val as Record<string, unknown>)) {
        if (!entry || typeof entry !== 'object' || typeof (entry as Record<string, unknown>).content !== 'string') {
            return false;
        }
    }
    return true;
}

function isStorageLayout(val: unknown): val is SolidityStorageLayout {
    if (!val || typeof val !== 'object') return false;
    const obj = val as Record<string, unknown>;
    return Array.isArray(obj.storage) && obj.storage.length > 0;
}
