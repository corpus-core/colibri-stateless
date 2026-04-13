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

import type { ContractCache, VerifiedContract } from './types.js';

const CACHE_PREFIX = 'c4x_';
const SAFE_KEY_RE = /^c4x_0x[0-9a-fA-F]{1,64}$/;

interface LocalStorageLike {
    getItem: (k: string) => string | null;
    setItem: (k: string, v: string) => void;
    removeItem: (k: string) => void;
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

function isNodeEnvironment(): boolean {
    return typeof process !== 'undefined'
        && typeof process.versions !== 'undefined'
        && typeof process.versions.node !== 'undefined';
}

function sanitizeKey(key: string): string {
    if (!SAFE_KEY_RE.test(key)) {
        throw new Error(`Invalid cache key: ${key}`);
    }
    return key;
}

/**
 * Create the default cache backed by localStorage (browser),
 * the filesystem (Node.js with `HOME` or `C4_STATE_DIR`),
 * or an in-memory Map fallback.
 */
export async function getDefaultCache(): Promise<ContractCache> {
    const ls = getLocalStorage();
    if (ls) {
        return {
            get: async (key: string) => ls.getItem(sanitizeKey(key)),
            set: async (key: string, value: string) => { ls.setItem(sanitizeKey(key), value); },
        };
    }

    if (isNodeEnvironment()) {
        const home = process.env.C4_STATE_DIR || (process.env.HOME ? undefined : null);
        if (home === null) {
            return createMemoryCache();
        }

        const fs = await import('fs');
        const pathMod = await import('path');
        const dir = home || pathMod.join(process.env.HOME!, '.colibri');
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
                fs.writeFileSync(resolved, value, 'utf-8');
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

/** Read a `VerifiedContract` from the cache by codeHash. */
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

/** Write a `VerifiedContract` to the cache by codeHash. */
export async function cacheSet(
    cache: ContractCache,
    codeHash: string,
    contract: VerifiedContract,
): Promise<void> {
    await cache.set(CACHE_PREFIX + codeHash, JSON.stringify(contract));
}

/** @deprecated Use `getDefaultCache` instead. */
export const get_default_cache = getDefaultCache;
