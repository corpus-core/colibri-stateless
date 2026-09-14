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

import type { CompilationInput, ContractCache, ContractMetadata, SolidityStorageLayout } from './types.js';
import {
    cacheGetCompilation, cacheGetMetadata, cacheSetCompilation, cacheSetMetadata,
} from './cache.js';

export type { CompilationInput } from './types.js';

const DEFAULT_BASE_URL = 'https://sourcify.dev/server';
const REQUEST_TIMEOUT_MS = 15_000;
const MAX_ATTEMPTS = 5;
const MAX_CONCURRENT = 4;
const RETRY_AFTER_MIN_MS = 1_000;
const RETRY_AFTER_MAX_MS = 60_000;
const BACKOFF_CAP_MS = 30_000;

const EMPTY_METADATA: ContractMetadata = { abi: null, sources: null, storageLayout: null };
const EMPTY_COMPILATION: CompilationInput = {
    stdJsonInput: null, compilerVersion: null, contractName: null, abi: null, sources: null,
};

const inflight = new Map<string, Promise<unknown>>();
let rateLimitedUntil = 0;
let activeRequests = 0;
const waitQueue: Array<() => void> = [];

let nowFn = (): number => Date.now();
let sleepFn = (ms: number): Promise<void> => new Promise(resolve => setTimeout(resolve, ms));

/**
 * Replace clock/sleep used by the Sourcify client. Test-only.
 *
 * @param now - Function returning the current epoch milliseconds
 * @param sleep - Function that waits `ms` milliseconds
 */
export function setSourcifyClockForTests(
    now?: () => number,
    sleep?: (ms: number) => Promise<void>,
): void {
    if (now) nowFn = now;
    if (sleep) sleepFn = sleep;
}

/**
 * Reset in-flight maps, rate-limit cooldown, and the default clock. Test-only.
 */
export function resetSourcifyStateForTests(): void {
    inflight.clear();
    rateLimitedUntil = 0;
    activeRequests = 0;
    waitQueue.length = 0;
    nowFn = () => Date.now();
    sleepFn = (ms: number) => new Promise(resolve => setTimeout(resolve, ms));
}

/**
 * Fetch contract metadata (ABI, source files, storage layout) from the
 * Sourcify v2 API. Returns null fields for unverified contracts.
 *
 * Successful responses and 404 misses are persisted when `cache` is provided.
 * Rate-limit and network failures are never written to the cache.
 *
 * @param address - Contract address (checksummed or lowercase)
 * @param chainId - EVM chain ID (e.g. 1 for Ethereum mainnet)
 * @param baseUrl - Override Sourcify server URL for self-hosted instances
 * @param cache - Optional persistent cache
 * @return Contract metadata with nullable fields
 */
export async function fetchContractMetadata(
    address: string,
    chainId: number,
    baseUrl?: string,
    cache?: ContractCache,
): Promise<ContractMetadata> {
    const addr = address.toLowerCase();
    const key = `meta:${baseUrl || ''}:${chainId}:${addr}`;

    if (cache) {
        const cached = await cacheGetMetadata(cache, chainId, addr);
        if (cached === 'empty') return EMPTY_METADATA;
        if (cached) return cached;
    }

    return dedupe(key, async () => {
        const result = await sourcifyRequest(buildUrl(baseUrl, chainId, address, 'abi,sources,storageLayout'), parseMetadata);
        if (cache) {
            if (result.kind === 'ok') {
                const empty = !result.value.abi && !result.value.sources;
                await cacheSetMetadata(cache, chainId, addr, empty ? 'empty' : result.value);
            } else if (result.kind === 'miss') {
                await cacheSetMetadata(cache, chainId, addr, 'empty');
            }
        }
        return result.kind === 'ok' ? result.value : EMPTY_METADATA;
    });
}

/**
 * Fetch compilation input (stdJsonInput, compiler version, contract name, ABI, sources)
 * from the Sourcify v2 API. Used for bytecode verification via `compileAndVerify`.
 *
 * Successful responses and 404 misses are persisted when `cache` is provided.
 * Rate-limit and network failures are never written to the cache.
 *
 * @param address - Contract address
 * @param chainId - EVM chain ID
 * @param baseUrl - Override Sourcify server URL
 * @param cache - Optional persistent cache
 * @return Compilation metadata with nullable fields
 */
export async function fetchCompilationInput(
    address: string,
    chainId: number,
    baseUrl?: string,
    cache?: ContractCache,
): Promise<CompilationInput> {
    const addr = address.toLowerCase();
    const key = `comp:${baseUrl || ''}:${chainId}:${addr}`;

    if (cache) {
        const cached = await cacheGetCompilation(cache, chainId, addr);
        if (cached === 'empty') return EMPTY_COMPILATION;
        if (cached) return cached;
    }

    return dedupe(key, async () => {
        const result = await sourcifyRequest(buildUrl(baseUrl, chainId, address, 'abi,stdJsonInput,compilation,sources'), parseCompilation);
        if (cache) {
            if (result.kind === 'ok') {
                const empty = !result.value.abi && !result.value.sources;
                await cacheSetCompilation(cache, chainId, addr, empty ? 'empty' : result.value);
            } else if (result.kind === 'miss') {
                await cacheSetCompilation(cache, chainId, addr, 'empty');
            }
        }
        return result.kind === 'ok' ? result.value : EMPTY_COMPILATION;
    });
}

type FetchResult<T> =
    | { kind: 'ok'; value: T }
    | { kind: 'miss' }
    | { kind: 'unavailable' };

async function dedupe<T>(key: string, fn: () => Promise<T>): Promise<T> {
    const existing = inflight.get(key);
    if (existing) return existing as Promise<T>;

    const promise = fn().finally(() => {
        if (inflight.get(key) === promise) inflight.delete(key);
    });
    inflight.set(key, promise);
    return promise;
}

function buildUrl(baseUrl: string | undefined, chainId: number, address: string, fields: string): string {
    const base = (baseUrl || DEFAULT_BASE_URL).replace(/\/$/, '');
    return `${base}/v2/contract/${chainId}/${address}?fields=${fields}`;
}

function clamp(ms: number, min: number, max: number): number {
    return Math.min(max, Math.max(min, ms));
}

/**
 * Parse a `Retry-After` header as seconds or an HTTP date.
 *
 * @param header - Raw header value
 * @return Delay in milliseconds, clamped to 1s–60s, or `null` if unusable
 */
export function parseRetryAfter(header: string | null): number | null {
    if (!header) return null;
    const trimmed = header.trim();
    if (!trimmed) return null;

    const seconds = Number(trimmed);
    if (Number.isFinite(seconds) && seconds >= 0) {
        return clamp(seconds * 1000, RETRY_AFTER_MIN_MS, RETRY_AFTER_MAX_MS);
    }

    const date = Date.parse(trimmed);
    if (!Number.isNaN(date)) {
        return clamp(date - nowFn(), RETRY_AFTER_MIN_MS, RETRY_AFTER_MAX_MS);
    }
    return null;
}

function backoffMs(attempt: number): number {
    const base = Math.min(1000 * (2 ** attempt), BACKOFF_CAP_MS);
    return clamp(base * (0.5 + Math.random() * 0.5), RETRY_AFTER_MIN_MS, BACKOFF_CAP_MS);
}

function isAlwaysRetryable(status: number): boolean {
    return status === 429 || status === 503;
}

function isConditionalRetryable(status: number): boolean {
    return status === 403 || status === 502;
}

function applyCooldown(ms: number): void {
    rateLimitedUntil = Math.max(rateLimitedUntil, nowFn() + ms);
}

async function waitForCooldown(): Promise<void> {
    const wait = rateLimitedUntil - nowFn();
    if (wait > 0) await sleepFn(wait);
}

async function acquireSlot(): Promise<void> {
    if (activeRequests < MAX_CONCURRENT) {
        activeRequests++;
        return;
    }
    await new Promise<void>(resolve => { waitQueue.push(resolve); });
}

function releaseSlot(): void {
    const next = waitQueue.shift();
    if (next) next();
    else activeRequests--;
}

async function sourcifyRequest<T>(
    url: string,
    parse: (body: Record<string, unknown>) => T,
): Promise<FetchResult<T>> {
    for (let attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
        await waitForCooldown();
        await acquireSlot();

        let response: Response;
        try {
            response = await fetch(url, { signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS) });
        } catch {
            releaseSlot();
            if (attempt + 1 >= MAX_ATTEMPTS) return { kind: 'unavailable' };
            const delay = backoffMs(attempt);
            applyCooldown(delay);
            await sleepFn(delay);
            continue;
        }

        releaseSlot();

        if (response.ok) {
            let body: unknown;
            try {
                body = await response.json();
            } catch {
                return { kind: 'unavailable' };
            }
            if (!body || typeof body !== 'object' || Array.isArray(body)) {
                return { kind: 'unavailable' };
            }
            return { kind: 'ok', value: parse(body as Record<string, unknown>) };
        }

        if (response.status === 404) return { kind: 'miss' };

        const retryAfter = parseRetryAfter(response.headers.get('Retry-After'));
        const retryable = isAlwaysRetryable(response.status)
            || (isConditionalRetryable(response.status) && retryAfter !== null);

        if (!retryable || attempt + 1 >= MAX_ATTEMPTS) {
            return { kind: 'unavailable' };
        }

        const delay = retryAfter ?? backoffMs(attempt);
        applyCooldown(delay);
        await sleepFn(delay);
    }

    return { kind: 'unavailable' };
}

function parseMetadata(body: Record<string, unknown>): ContractMetadata {
    const abi = Array.isArray(body.abi) ? body.abi : null;
    const sources = isSourcesObject(body.sources) ? body.sources : null;
    const storageLayout = isStorageLayout(body.storageLayout)
        ? body.storageLayout as SolidityStorageLayout
        : null;
    return { abi, sources, storageLayout };
}

function parseCompilation(body: Record<string, unknown>): CompilationInput {
    const abi = Array.isArray(body.abi) ? body.abi : null;
    const sources = isSourcesObject(body.sources) ? body.sources : null;
    const stdJsonInput = (body.stdJsonInput && typeof body.stdJsonInput === 'object')
        ? body.stdJsonInput as Record<string, unknown>
        : null;
    const compilation = body.compilation as Record<string, unknown> | undefined;
    const compilerVersion = (typeof compilation?.compilerVersion === 'string')
        ? compilation.compilerVersion
        : null;
    const contractName = (typeof compilation?.name === 'string')
        ? compilation.name
        : null;
    return { stdJsonInput, compilerVersion, contractName, abi, sources };
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
