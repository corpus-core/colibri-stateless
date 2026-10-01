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

import type { CompilationInput, ContractCache, ContractMetadata, SolidityStorageLayout, TokenInfo, VerifiedContract } from './types.js';
import { keccak256, toUtf8Bytes } from 'ethers';

const CACHE_PREFIX = 'c4x_';

/**
 * Allowed persistent cache filenames.
 * - `c4x_0x{hex}` — verified contract by codeHash
 * - `c4l_0x{hex}` — skeleton storage layout by sources fingerprint
 * - `c4s_{chainId}_{address}` — Sourcify compilation input (or miss marker)
 * - `c4m_{chainId}_{address}` — Sourcify metadata (or miss marker)
 * - `c4e_{chainId}_{address}` — resolved ERC-20 symbol and decimals
 */
const SAFE_KEY_RE = /^(?:c4[xl]_0x[0-9a-fA-F]{1,64}|c4[sme]_\d+_0x[0-9a-fA-F]{1,40})$/;

/** Symbol strings safe to embed in a prompt. Rejects whitespace and control chars. */
const TOKEN_SYMBOL_RE = /^[A-Za-z0-9._$-]{1,32}$/;

/** Bump to invalidate checked-in skeleton layouts after extractor changes. */
const LAYOUT_CACHE_VERSION = 2;

const ADDRESS_RE = /^0x[0-9a-f]{1,40}$/;
const MISS_MARKER = '{"empty":true}';

interface LocalStorageLike {
    getItem: (k: string) => string | null;
    setItem: (k: string, v: string) => void;
    removeItem: (k: string) => void;
    readonly length: number;
    key: (index: number) => string | null;
}

function getLocalStorage(): LocalStorageLike | null {
    try {
        const ls = (globalThis as Record<string, unknown>).localStorage as LocalStorageLike | undefined;
        if (!ls) return null;
        const key = '__c4x_probe__';
        ls.setItem(key, '1');
        ls.getItem(key);
        ls.removeItem(key);
        return ls;
    } catch {
        return null;
    }
}

/**
 * Minimal shape of the Web Platform Cache Storage API we rely on. The real
 * `CacheStorage` interface has more methods; we only need `open`.
 */
interface CachesLike {
    open(name: string): Promise<CacheLike>;
}

/**
 * Minimal shape of a `Cache` object. `match` returns `undefined` for a miss,
 * `put` stores a `Response` under a key (`Request` or URL string).
 */
interface CacheLike {
    match(request: Request | string): Promise<Response | undefined>;
    put(request: Request | string, response: Response): Promise<void>;
    delete(request: Request | string): Promise<boolean>;
}

/**
 * The Cache API is virtually unlimited (bounded by the browser's per-origin
 * disk quota, usually tens of MB to several GB) and is the right backend for
 * verified-contract source bundles, which quickly exceed localStorage's hard
 * ~5 MB quota.
 *
 * Available in secure contexts (`https://`, `localhost`, service workers,
 * most Chromium/Firefox/Safari builds). Returns `null` when the API is
 * missing (older browsers, `file://`, insecure HTTP, Node).
 *
 * @return `caches` global, or `null` when it is not available
 */
function getCachesApi(): CachesLike | null {
    try {
        const c = (globalThis as Record<string, unknown>).caches as CachesLike | undefined;
        if (!c || typeof c.open !== 'function') return null;
        // Response is also required (we wrap values into one). Browsers that
        // ship `caches` always ship `Response`, so this is just a sanity gate.
        if (typeof (globalThis as Record<string, unknown>).Response !== 'function') return null;
        return c;
    } catch {
        return null;
    }
}

/** Cache name under `caches`. Bump the suffix to invalidate all entries. */
const CACHE_API_NAME = 'c4-explainer-v1';
/**
 * URL prefix for Cache API keys. The pathname carries the sanitized cache key.
 * The host is a non-routable `.local` label so a stray `fetch(url)` on this
 * value could never leave the browser.
 */
const CACHE_API_URL_PREFIX = 'https://c4-explainer.local/';

/**
 * Legacy localStorage key prefixes written by earlier versions of the
 * explainer before the switch to the Cache Storage API. Any entry with one
 * of these prefixes is orphaned once we are on the Cache API and only wastes
 * the (tight) 5 MB per-origin localStorage quota.
 */
const LEGACY_LS_PREFIXES = ['c4x_', 'c4l_', 'c4s_', 'c4m_', 'c4e_'];

/**
 * Delete every orphaned `c4*_` entry from `localStorage`. Called exactly once
 * after we have decided on the Cache API as the primary backend. Fully
 * best-effort: a missing or quota-locked `localStorage` is silently ignored.
 */
function reclaimLegacyLocalStorage(): void {
    const ls = getLocalStorage();
    if (!ls) return;
    try {
        const victims: string[] = [];
        for (let i = 0; i < ls.length; i++) {
            const key = ls.key(i);
            if (!key) continue;
            if (LEGACY_LS_PREFIXES.some(p => key.startsWith(p))) victims.push(key);
        }
        for (const key of victims) {
            try { ls.removeItem(key); } catch { /* ignore */ }
        }
    } catch {
        // Not fatal -- the Cache API still works even if we fail to free the
        // old localStorage space.
    }
}

/**
 * Build a `ContractCache` backed by the Web Platform Cache Storage API.
 *
 * The returned backend wraps string values into `Response` objects. Keys are
 * sanitized via `sanitizeKey` so a caller cannot smuggle an unexpected URL
 * into the cache storage.
 *
 * @return Cache backend, or `null` when the Cache API is not available
 */
async function createCacheApiCache(): Promise<ContractCache | null> {
    const caches = getCachesApi();
    if (!caches) return null;
    let cache: CacheLike;
    try {
        cache = await caches.open(CACHE_API_NAME);
    } catch {
        return null;
    }
    const urlFor = (key: string): string => CACHE_API_URL_PREFIX + sanitizeKey(key);
    return {
        get: async (key: string) => {
            try {
                const response = await cache.match(urlFor(key));
                if (!response) return null;
                return await response.text();
            } catch {
                return null;
            }
        },
        set: async (key: string, value: string) => {
            try {
                const ResponseCtor = (globalThis as Record<string, unknown>).Response as {
                    new(body?: BodyInit | null, init?: ResponseInit): Response;
                };
                await cache.put(
                    urlFor(key),
                    new ResponseCtor(value, { headers: { 'content-type': 'application/json' } }),
                );
            } catch {
                // Quota exhausted, parse error on the key, or the browser evicted
                // the cache storage between `open` and `put`. The cache is
                // best-effort; a missed write only means the next read misses.
            }
        },
    };
}

function isNodeEnvironment(): boolean {
    return typeof process !== 'undefined'
        && typeof process.versions !== 'undefined'
        && typeof process.versions.node !== 'undefined';
}

/**
 * Validate a cache key. Rejects path traversal and unexpected prefixes.
 *
 * @param key - Raw cache key
 * @return The same key if it is safe
 */
export function sanitizeKey(key: string): string {
    if (!SAFE_KEY_RE.test(key)) {
        throw new Error(`Invalid cache key: ${key}`);
    }
    return key;
}

function normalizeAddress(address: string): string | null {
    const addr = address.toLowerCase();
    return ADDRESS_RE.test(addr) ? addr : null;
}

function sourcifyKey(prefix: 'c4s' | 'c4m' | 'c4e', chainId: number, address: string): string | null {
    if (!Number.isInteger(chainId) || chainId < 0 || chainId > 0xffffffff) return null;
    const addr = normalizeAddress(address);
    if (!addr) return null;
    const key = `${prefix}_${chainId}_${addr}`;
    return SAFE_KEY_RE.test(key) ? key : null;
}

/**
 * Cache key for a Sourcify compilation-input entry.
 *
 * @param chainId - EVM chain ID
 * @param address - Contract address
 * @return Safe key, or `null` if the inputs cannot be used as a filename
 */
export function sourcifyCompilationKey(chainId: number, address: string): string | null {
    return sourcifyKey('c4s', chainId, address);
}

/**
 * Cache key for a Sourcify metadata entry.
 *
 * @param chainId - EVM chain ID
 * @param address - Contract address
 * @return Safe key, or `null` if the inputs cannot be used as a filename
 */
export function sourcifyMetadataKey(chainId: number, address: string): string | null {
    return sourcifyKey('c4m', chainId, address);
}

/**
 * `true` when `symbol` is safe to embed in a prompt.
 *
 * Allows letters, digits, `.`, `_`, `$`, and `-`, at most 32 characters.
 * Rejects whitespace, quotes, and newlines so a token symbol cannot break
 * the address directory or inject instructions.
 *
 * @param symbol - Candidate ERC-20 symbol
 * @return Whether the symbol may be stored and shown
 */
export function isSafeTokenSymbol(symbol: string): boolean {
    return TOKEN_SYMBOL_RE.test(symbol);
}

/**
 * Cache key for a resolved ERC-20 `symbol()` / `decimals()` pair.
 *
 * Keyed by chain and address, not by code hash: the same bytecode can store
 * a different symbol on every proxy.
 *
 * @param chainId - EVM chain ID
 * @param address - Token address
 * @return Safe `c4e_…` key, or `null` if the inputs cannot be used as a filename
 */
export function tokenCacheKey(chainId: number, address: string): string | null {
    return sourcifyKey('c4e', chainId, address);
}

/**
 * Cache key for a skeleton storage layout. Hash of canonical sources +
 * contract name so two addresses with the same files share an entry, and a
 * Sourcify source update misses.
 *
 * @param sources - Solidity sources as `{ file: { content } }`
 * @param contractName - Target contract, if known
 * @return Safe `c4l_0x…` key, or `null` if the fingerprint cannot be hashed
 */
export function layoutCacheKey(
    sources: Record<string, { content: string }>,
    contractName?: string,
): string | null {
    const files = Object.keys(sources).sort().map(name => [name, sources[name]?.content ?? '']);
    const payload = JSON.stringify({ v: LAYOUT_CACHE_VERSION, n: contractName ?? '', files });
    const key = `c4l_${keccak256(toUtf8Bytes(payload))}`;
    return SAFE_KEY_RE.test(key) ? key : null;
}

/**
 * Resolve the Node.js cache directory (`C4_STATE_DIR` or `~/.colibri`).
 *
 * @return Absolute directory path, or `null` in the browser / when neither env is set
 */
export async function getCacheDirectory(): Promise<string | null> {
    if (!isNodeEnvironment()) return null;
    if (process.env.C4_STATE_DIR) return process.env.C4_STATE_DIR;
    if (!process.env.HOME) return null;
    const pathMod = await import('path');
    return pathMod.join(process.env.HOME, '.colibri');
}

/**
 * Create the default cache for the current environment.
 *
 * Priority (first available wins):
 *
 * 1. **Cache Storage API** (browser, secure context) — virtually unlimited,
 *    async. Preferred because `localStorage`'s hard ~5 MB per-origin quota
 *    overflows quickly once the explainer caches the Sourcify source bundles
 *    of a handful of contracts (`QuotaExceededError` from `setItem`).
 * 2. **`localStorage`** (older browsers / insecure contexts) — same shape as
 *    before, kept as a graceful fallback.
 * 3. **Filesystem** (Node.js with `C4_STATE_DIR` or `HOME`) — persistent cache
 *    for CLI / server usage.
 * 4. **In-memory `Map`** — last resort, resets on reload.
 *
 * @return Cache implementation for the current environment
 */
export async function getDefaultCache(): Promise<ContractCache> {
    const cacheApi = await createCacheApiCache();
    if (cacheApi) {
        // Reclaim localStorage: once we are on the Cache API, any c4* entries
        // left in localStorage from a previous version are orphaned and keep
        // consuming the (tight) 5 MB per-origin quota for other apps on the
        // same origin. Fire-and-forget, failures are not fatal.
        reclaimLegacyLocalStorage();
        return cacheApi;
    }

    const ls = getLocalStorage();
    if (ls) {
        return {
            get: async (key: string) => ls.getItem(sanitizeKey(key)),
            set: async (key: string, value: string) => {
                try {
                    ls.setItem(sanitizeKey(key), value);
                } catch {
                    // QuotaExceededError or SecurityError -- fall back to
                    // silently skipping the write. The next read misses, the
                    // enrichment pipeline re-fetches, and the LLM still runs.
                }
            },
        };
    }

    const dir = await getCacheDirectory();
    if (dir) {
        const fs = await import('fs');
        const pathMod = await import('path');
        try { fs.mkdirSync(dir, { recursive: true }); } catch { /* exists */ }

        return {
            get: async (key: string) => {
                const resolved = pathMod.resolve(dir, sanitizeKey(key));
                if (!resolved.startsWith(pathMod.resolve(dir) + pathMod.sep)) return null;
                try { return fs.readFileSync(resolved, 'utf-8'); } catch { return null; }
            },
            set: async (key: string, value: string) => {
                const resolved = pathMod.resolve(dir, sanitizeKey(key));
                if (!resolved.startsWith(pathMod.resolve(dir) + pathMod.sep)) return;
                try { fs.writeFileSync(resolved, value, 'utf-8'); } catch { /* quota / permissions */ }
            },
        };
    }

    return createMemoryCache();
}

function createMemoryCache(): ContractCache {
    const mem = new Map<string, string>();
    return {
        get: async (key: string) => mem.get(key) ?? null,
        set: async (key: string, value: string) => { mem.set(key, value); },
    };
}

function isVerifiedContract(obj: unknown): obj is VerifiedContract {
    if (!obj || typeof obj !== 'object') return false;
    const o = obj as Record<string, unknown>;
    return Array.isArray(o.abi)
        && typeof o.compilerVersion === 'string'
        && typeof o.contractName === 'string';
}

function isMissMarker(obj: unknown): boolean {
    return !!obj && typeof obj === 'object' && (obj as Record<string, unknown>).empty === true;
}

/**
 * Read a `VerifiedContract` from the cache by codeHash.
 *
 * @param cache - Cache backend
 * @param codeHash - keccak256 of deployed runtime bytecode
 * @return Cached contract, or `null` on miss / corruption
 */
export async function cacheGet(
    cache: ContractCache,
    codeHash: string,
): Promise<VerifiedContract | null> {
    const raw = await cache.get(CACHE_PREFIX + codeHash);
    if (!raw) return null;
    try {
        const parsed = JSON.parse(raw);
        return isVerifiedContract(parsed) ? parsed : null;
    } catch {
        return null;
    }
}

/**
 * Write a `VerifiedContract` to the cache by codeHash.
 *
 * @param cache - Cache backend
 * @param codeHash - keccak256 of deployed runtime bytecode
 * @param contract - Verified metadata to persist
 */
export async function cacheSet(
    cache: ContractCache,
    codeHash: string,
    contract: VerifiedContract,
): Promise<void> {
    await cache.set(CACHE_PREFIX + codeHash, JSON.stringify(contract));
}

/**
 * Read a cached Sourcify compilation input.
 *
 * @param cache - Cache backend
 * @param chainId - EVM chain ID
 * @param address - Contract address
 * @return `'empty'` for a persisted miss, the input on hit, or `null` if uncached
 */
export async function cacheGetCompilation(
    cache: ContractCache,
    chainId: number,
    address: string,
): Promise<CompilationInput | 'empty' | null> {
    const key = sourcifyCompilationKey(chainId, address);
    if (!key) return null;
    const raw = await cache.get(key);
    if (!raw) return null;
    try {
        const parsed = JSON.parse(raw);
        if (isMissMarker(parsed)) return 'empty';
        if (!parsed || typeof parsed !== 'object') return null;
        return parsed as CompilationInput;
    } catch {
        return null;
    }
}

/**
 * Persist a Sourcify compilation input or an explicit miss marker.
 *
 * @param cache - Cache backend
 * @param chainId - EVM chain ID
 * @param address - Contract address
 * @param value - Compilation input, or `'empty'` for a verified miss (404 / no ABI)
 */
export async function cacheSetCompilation(
    cache: ContractCache,
    chainId: number,
    address: string,
    value: CompilationInput | 'empty',
): Promise<void> {
    const key = sourcifyCompilationKey(chainId, address);
    if (!key) return;
    try {
        await cache.set(key, value === 'empty' ? MISS_MARKER : JSON.stringify(value));
    } catch { /* quota */ }
}

/**
 * Read cached Sourcify contract metadata.
 *
 * @param cache - Cache backend
 * @param chainId - EVM chain ID
 * @param address - Contract address
 * @return `'empty'` for a persisted miss, metadata on hit, or `null` if uncached
 */
export async function cacheGetMetadata(
    cache: ContractCache,
    chainId: number,
    address: string,
): Promise<ContractMetadata | 'empty' | null> {
    const key = sourcifyMetadataKey(chainId, address);
    if (!key) return null;
    const raw = await cache.get(key);
    if (!raw) return null;
    try {
        const parsed = JSON.parse(raw);
        if (isMissMarker(parsed)) return 'empty';
        if (!parsed || typeof parsed !== 'object') return null;
        return parsed as ContractMetadata;
    } catch {
        return null;
    }
}

/**
 * Persist Sourcify contract metadata or an explicit miss marker.
 *
 * @param cache - Cache backend
 * @param chainId - EVM chain ID
 * @param address - Contract address
 * @param value - Metadata, or `'empty'` for a verified miss
 */
export async function cacheSetMetadata(
    cache: ContractCache,
    chainId: number,
    address: string,
    value: ContractMetadata | 'empty',
): Promise<void> {
    const key = sourcifyMetadataKey(chainId, address);
    if (!key) return;
    try {
        await cache.set(key, value === 'empty' ? MISS_MARKER : JSON.stringify(value));
    } catch { /* quota */ }
}

function isCachedToken(val: unknown): val is TokenInfo {
    if (!val || typeof val !== 'object') return false;
    const o = val as Record<string, unknown>;
    return typeof o.symbol === 'string'
        && isSafeTokenSymbol(o.symbol)
        && typeof o.decimals === 'number'
        && Number.isInteger(o.decimals)
        && o.decimals >= 0
        && o.decimals <= 255;
}

/**
 * Read a cached ERC-20 symbol and decimals.
 *
 * @param cache - Cache backend
 * @param chainId - EVM chain ID
 * @param address - Token address
 * @return Token info on hit, or `null` on miss / corruption / unsafe symbol
 */
export async function cacheGetToken(
    cache: ContractCache,
    chainId: number,
    address: string,
): Promise<TokenInfo | null> {
    const key = tokenCacheKey(chainId, address);
    if (!key) return null;
    const raw = await cache.get(key);
    if (!raw) return null;
    try {
        const parsed = JSON.parse(raw);
        return isCachedToken(parsed) ? parsed : null;
    } catch {
        return null;
    }
}

/**
 * Persist a resolved ERC-20 symbol and decimals.
 *
 * Unsafe symbols and decimals outside `0..255` are not stored, so a later
 * call can try again.
 *
 * @param cache - Cache backend
 * @param chainId - EVM chain ID
 * @param address - Token address
 * @param token - Validated symbol and decimals
 */
export async function cacheSetToken(
    cache: ContractCache,
    chainId: number,
    address: string,
    token: TokenInfo,
): Promise<void> {
    if (!isCachedToken(token)) return;
    const key = tokenCacheKey(chainId, address);
    if (!key) return;
    try {
        await cache.set(key, JSON.stringify({ symbol: token.symbol, decimals: token.decimals }));
    } catch { /* quota */ }
}

function isCachedStorageLayout(val: unknown): val is SolidityStorageLayout {
    if (!val || typeof val !== 'object') return false;
    return Array.isArray((val as Record<string, unknown>).storage);
}

/**
 * Read a cached skeleton storage layout.
 *
 * @param cache - Cache backend
 * @param sources - Solidity sources used to build the skeleton
 * @param contractName - Target contract, if known
 * @return Layout on hit, or `null` on miss / corruption
 */
export async function cacheGetLayout(
    cache: ContractCache,
    sources: Record<string, { content: string }>,
    contractName?: string,
): Promise<SolidityStorageLayout | null> {
    const key = layoutCacheKey(sources, contractName);
    if (!key) return null;
    const raw = await cache.get(key);
    if (!raw) return null;
    try {
        const parsed = JSON.parse(raw);
        return isCachedStorageLayout(parsed) ? parsed : null;
    } catch {
        return null;
    }
}

/**
 * Persist a skeleton storage layout. Failed extractions (`null`) are not
 * stored so a later extractor fix can retry.
 *
 * @param cache - Cache backend
 * @param sources - Solidity sources used to build the skeleton
 * @param contractName - Target contract, if known
 * @param layout - solc storage layout
 */
export async function cacheSetLayout(
    cache: ContractCache,
    sources: Record<string, { content: string }>,
    contractName: string | undefined,
    layout: SolidityStorageLayout,
): Promise<void> {
    if (!isCachedStorageLayout(layout)) return;
    const key = layoutCacheKey(sources, contractName);
    if (!key) return;
    try {
        await cache.set(key, JSON.stringify(layout));
    } catch { /* quota */ }
}

/** @deprecated Use `getDefaultCache` instead. */
export const get_default_cache = getDefaultCache;
