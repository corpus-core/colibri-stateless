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

import { keccak256 } from 'ethers';
import { getCacheDirectory } from './cache.js';
import { elapsedMs, explainerLog } from './log.js';

interface SolcInstance {
    compile(input: string): string;
    compileAsync?(input: string): Promise<string>;
    version(): string;
}

interface CompileResult {
    verified: boolean;
    abi: unknown[] | null;
    sources: Record<string, { content: string }> | null;
    /**
     * Runtime bytecode + `sourceMap` string of the verified contract, when the
     * caller passed `includeSourceMap: true` and the compilation succeeded.
     * Callers use these to map executed JUMPDEST PCs to Solidity ranges.
     */
    sourceMap?: {
        sourceMap: string;
        runtimeBytecode: string;
        sourceIndex: Map<number, string>;
    } | null;
}

/** Additional compilation controls. */
export interface CompileOptions {
    /** Request `evm.deployedBytecode.sourceMap` and expose the runtime bytecode. */
    includeSourceMap?: boolean;
    /**
     * Called when the full `keccak256(deployedBytecode)` comparison does not
     * match — provides the on-chain runtime bytecode so `compileAndVerify` can
     * strip the CBOR metadata trailer from both sides and fall back to a
     * Sourcify-style **partial match**.
     *
     * The returned bytecode is only trusted after `keccak256(onChain)` has
     * been checked against `expectedCodeHash`; a mismatch aborts verification.
     *
     * Returning `null` disables the fallback and keeps verification strict.
     */
    fetchOnChainBytecode?: () => Promise<string | null>;
}

const MAX_CACHED_COMPILERS = 1;
const SOLC_WASM_BASE = 'https://binaries.soliditylang.org/wasm';
const SOLC_BIN_BASE = 'https://binaries.soliditylang.org/bin';
/**
 * npm `solc` release used for skeleton layout.
 * Must stay equal to the installed `solc` package version (asserted in tests).
 * The browser cannot `import('solc')`: `soljson.js` assigns `module.exports`
 * only when `process.versions.node` is set, so the page loads this same release
 * from `binaries.soliditylang.org` into a worker instead.
 */
export const BUNDLED_SOLC_RELEASE = '0.8.36';
/** Reject a compromised list.json entry or an unexpectedly huge binary. */
const SOLJSON_FILE_RE = /^soljson-v\d+\.\d+\.\d+\+commit\.[0-9a-f]{6,10}\.js$/;
const MAX_SOLJSON_CHARS = 30_000_000;

/** Process events Emscripten soljson typically registers (each holds the wasm heap). */
const SOLC_PROCESS_EVENTS = [
    'uncaughtException',
    'unhandledRejection',
    'exit',
    'beforeExit',
    'warning',
] as const;

interface CachedCompiler {
    compiler: SolcInstance;
    release: () => void;
}

const compilerCache = new Map<string, CachedCompiler>();
const compilerLoads = new Map<string, Promise<SolcInstance>>();
let bundledVersion: string | null = null;
let browserBundledLoad: Promise<CachedCompiler> | null = null;

const COMPILER_VERSION_RE = /^v?\d+\.\d+\.\d+\+commit\.[0-9a-f]{6,10}(\.Emscripten\.clang)?$/;
const HEX_BYTECODE_RE = /^(?:0x)?[0-9a-fA-F]+$/;

function extractShortVersion(fullVersion: string): string {
    const match = fullVersion.match(/^(\d+\.\d+\.\d+\+commit\.[a-f0-9]+)/);
    return match?.[1] ?? fullVersion;
}

function validateCompilerVersion(version: string): void {
    if (!COMPILER_VERSION_RE.test(version)) {
        throw new Error(`Invalid compiler version format: ${version}`);
    }
}

/** Drop the oldest cached remote compiler (cap 1) and release its heap / worker. */
function evictOldestCompiler(): void {
    if (compilerCache.size >= MAX_CACHED_COMPILERS) {
        const oldest = compilerCache.keys().next().value;
        if (oldest) {
            const entry = compilerCache.get(oldest);
            compilerCache.delete(oldest);
            entry?.release();
        }
    }
}

/** Release every cached compiler. Test-only via `resetCompilerStateForTests`. */
function releaseAllCompilers(): void {
    for (const entry of compilerCache.values()) entry.release();
    compilerCache.clear();
    const pendingBrowser = browserBundledLoad;
    browserBundledLoad = null;
    if (pendingBrowser) void pendingBrowser.then(entry => entry.release()).catch(() => { /* load already failed */ });
}

/**
 * Get the bundled solc compiler (shipped with the `solc` npm package).
 *
 * On Node this is `import('solc')`. In the browser that import does not yield a
 * compiler: `soljson.js` writes `module.exports` only under Node. The browser
 * path downloads the wasm build of `BUNDLED_SOLC_RELEASE` from
 * `binaries.soliditylang.org` and runs it in a worker (`compileAsync`).
 *
 * @return Bundled compiler instance
 */
export async function getBundledCompiler(): Promise<SolcInstance> {
    if (!isNodeEnvironment()) {
        if (!browserBundledLoad) {
            browserBundledLoad = loadBundledCompilerBrowser().catch(err => {
                browserBundledLoad = null;
                throw err;
            });
        }
        const loaded = await browserBundledLoad;
        bundledVersion = extractShortVersion(loaded.compiler.version());
        return loaded.compiler;
    }
    const solc = await import('solc');
    const compiler = solc.default as unknown as SolcInstance;
    bundledVersion = extractShortVersion(compiler.version());
    return compiler;
}

/**
 * Filesystem name for a cached solc JS binary (`soljson-vX.Y.Z+commit....js`).
 *
 * @param version - Compiler version string (with or without leading `v`)
 * @return Safe filename derived from the validated version
 */
export function solcCacheFileName(version: string): string {
    validateCompilerVersion(version);
    const versionStr = version.startsWith('v') ? version : 'v' + version;
    const fileName = `soljson-${versionStr}.js`;
    if (fileName.includes('..') || fileName.includes('/') || fileName.includes('\\')) {
        throw new Error(`Invalid compiler cache filename: ${fileName}`);
    }
    return fileName;
}

/**
 * Drop in-memory compiler instances and in-flight loads. Test-only.
 */
export function resetCompilerStateForTests(): void {
    releaseAllCompilers();
    compilerLoads.clear();
    bundledVersion = null;
}

/**
 * Number of compiler versions currently retained in memory (cap 1). Test-only.
 *
 * @return Cache size
 */
export function compilerCacheSizeForTests(): number {
    return compilerCache.size;
}

/**
 * Insert a dummy compiler so eviction/release can be tested without a real soljson.
 *
 * @param version - Cache key (short version string)
 * @param release - Called when this entry is evicted or reset
 */
export function installDummyCompilerForTests(version: string, release: () => void): void {
    evictOldestCompiler();
    compilerCache.set(version, { compiler: { compile: () => '{}', version: () => version }, release });
}

function isNodeEnvironment(): boolean {
    return typeof process !== 'undefined'
        && typeof process.versions !== 'undefined'
        && typeof process.versions.node !== 'undefined';
}

/**
 * Load a specific solc compiler version. If the version matches the bundled
 * compiler, returns it directly; otherwise loads from the on-disk cache
 * (Node.js) or downloads the official JS binary.
 *
 * Node downloads prefer `binaries.soliditylang.org/wasm/` (needed for 0.4.x on
 * modern V8) and fall back to `/bin/` if the wasm build is missing. Disk cache
 * lives under `{cacheDir}/solc/wasm/` so older asm.js files are not reused.
 * Remote compilers run in a worker thread on Node (`compileAsync`).
 * In the browser they run in a dedicated worker as well: the npm `solc` package
 * cannot be imported there, so the official soljson binary is fetched and
 * evaluated with `importScripts` inside that worker.
 * Emscripten heaps are not reclaimable in-process. At most one remote version
 * is kept; switching versions terminates the previous worker.
 * Set `C4_SOLC_IN_PROCESS=1` to load soljson in the main thread instead.
 *
 * @param version - Compiler version string, e.g. `"0.8.19+commit.7dd6d404"` or `"v0.8.19+commit.7dd6d404"`
 * @return Loaded compiler instance
 */
export async function loadCompiler(version: string): Promise<SolcInstance> {
    validateCompilerVersion(version);

    const normalizedVersion = version.startsWith('v') ? version.slice(1) : version;
    const shortVersion = extractShortVersion(normalizedVersion);

    const cached = compilerCache.get(shortVersion);
    if (cached) {
        explainerLog('debug', 'compiler cache hit', { scope: 'solc', version: shortVersion });
        return cached.compiler;
    }

    const inflight = compilerLoads.get(shortVersion);
    if (inflight) return inflight;

    const promise = loadCompilerUncached(version, shortVersion).finally(() => {
        if (compilerLoads.get(shortVersion) === promise) compilerLoads.delete(shortVersion);
    });
    compilerLoads.set(shortVersion, promise);
    return promise;
}

async function loadCompilerUncached(version: string, shortVersion: string): Promise<SolcInstance> {
    const cached = compilerCache.get(shortVersion);
    if (cached) return cached.compiler;

    const bundled = await getBundledCompiler();
    if (shortVersion === bundledVersion) {
        compilerCache.set(shortVersion, { compiler: bundled, release: () => { /* bundled solc is process-lifetime */ } });
        return bundled;
    }

    const versionStr = version.startsWith('v') ? version : 'v' + version;
    const loaded = isNodeEnvironment()
        ? await loadRemoteCompilerNode(versionStr)
        : await loadRemoteCompilerBrowser(versionStr);

    evictOldestCompiler();
    compilerCache.set(shortVersion, loaded);
    explainerLog('info', 'compiler loaded', { scope: 'solc', version: shortVersion });
    return loaded.compiler;
}

async function resolveSolcDiskPath(versionStr: string): Promise<string | null> {
    const dir = await getCacheDirectory();
    if (!dir) return null;
    const pathMod = await import('path');
    const fileName = solcCacheFileName(versionStr);
    const solcDir = pathMod.resolve(dir, 'solc', 'wasm');
    const resolved = pathMod.resolve(solcDir, fileName);
    if (!resolved.startsWith(solcDir + pathMod.sep)) return null;
    return resolved;
}

/**
 * keccak256 of deployed runtime bytecode. Returns `null` for library
 * placeholders, odd-length hex, or any other non-hex payload so callers
 * never throw on Sourcify/solc output.
 *
 * @param bytecodeHex - Runtime bytecode, with or without `0x`
 * @return Hash, or `null` if the input is not clean hex
 */
export function hashRuntimeBytecode(bytecodeHex: string): string | null {
    if (!bytecodeHex || !HEX_BYTECODE_RE.test(bytecodeHex)) return null;
    const hex = bytecodeHex.startsWith('0x') || bytecodeHex.startsWith('0X')
        ? bytecodeHex.slice(2)
        : bytecodeHex;
    if (hex.length === 0 || hex.length % 2 !== 0) return null;
    try {
        return keccak256('0x' + hex);
    } catch {
        return null;
    }
}

/**
 * Strip the Solidity CBOR metadata trailer from a runtime bytecode.
 *
 * solc appends the CBOR-encoded metadata hash at the end of the deployed
 * bytecode, followed by two bytes carrying the metadata length (big-endian
 * `uint16`). The appended block depends on exact source bytes, file paths
 * and compiler settings, so Sourcify "partial matches" differ from the
 * on-chain code **only** in this trailer.
 *
 * Example (solc 0.4.x): `...a1 65 'bzzr0' 58 20 <32-byte hash> 00 29`
 * - `00 29` = length `0x29 = 41` of the CBOR metadata.
 * - The 41 preceding bytes are the metadata (CBOR map with the swarm/ipfs hash).
 *
 * Returns the input unchanged (minus optional `0x` prefix) when the trailer
 * cannot be parsed or would extend past the start of the bytecode, so callers
 * can still fall back to a plain full-match comparison.
 *
 * @param bytecodeHex - Runtime bytecode, with or without `0x`
 * @return `0x`-prefixed lowercase hex without the trailer, or `null` on bad input
 */
export function stripCborMetadata(bytecodeHex: string): string | null {
    if (!bytecodeHex || !HEX_BYTECODE_RE.test(bytecodeHex)) return null;
    const hex = (bytecodeHex.startsWith('0x') ? bytecodeHex.slice(2) : bytecodeHex).toLowerCase();
    if (hex.length === 0 || hex.length % 2 !== 0) return null;
    // Need at least the 2-byte length suffix to even look at the trailer.
    if (hex.length < 4) return '0x' + hex;
    const metaLen = parseInt(hex.slice(-4), 16);
    if (!Number.isFinite(metaLen) || metaLen <= 0) return '0x' + hex;
    const suffixHex = (metaLen + 2) * 2;
    if (hex.length <= suffixHex) return '0x' + hex;
    return '0x' + hex.slice(0, hex.length - suffixHex);
}

interface NodeModuleInstance {
    _compile(source: string, filename: string): void;
    exports: unknown;
    parent?: { children?: unknown[] };
}

type ProcessListenerFn = (...args: unknown[]) => unknown;
type ProcessListenerMap = Map<string, ProcessListenerFn[]>;

interface ProcessLike {
    listeners(event: string): ProcessListenerFn[];
    removeListener(event: string, listener: ProcessListenerFn): void;
    on?: (event: string, listener: ProcessListenerFn) => unknown;
    addListener?: (event: string, listener: ProcessListenerFn) => unknown;
    once?: (event: string, listener: ProcessListenerFn) => unknown;
    prependListener?: (event: string, listener: ProcessListenerFn) => unknown;
    prependOnceListener?: (event: string, listener: ProcessListenerFn) => unknown;
}

interface NodeModuleNamespace {
    Module: {
        new(filename: string, parent?: unknown): NodeModuleInstance;
        _cache?: Record<string, unknown>;
    };
}

/**
 * Narrow `process` to the listener APIs we need. Returns null in the browser.
 *
 * @return Process listener surface, or `null` if unavailable
 */
function nodeProcess(): ProcessLike | null {
    if (typeof process === 'undefined') return null;
    const p = process as unknown as ProcessLike;
    if (typeof p.listeners !== 'function' || typeof p.removeListener !== 'function') return null;
    return p;
}

const PROCESS_ON_METHODS = ['on', 'addListener', 'once', 'prependListener', 'prependOnceListener'] as const;

/**
 * Record every `process.on` / `once` / `addListener` registration while
 * soljson initializes. Emscripten often registers handlers during the first
 * `version()` / `compile()`, not during `_compile`.
 *
 * @return `finish()` restores the original methods and returns the captured listeners
 */
function interceptProcessListeners(): { finish: () => ProcessListenerMap } {
    const added: ProcessListenerMap = new Map();
    const p = process as unknown as ProcessLike & Record<string, unknown>;
    const originals = new Map<string, { fn: (...args: unknown[]) => unknown; owned: boolean }>();

    const track = (event: unknown, fn: unknown): void => {
        if (typeof event !== 'string' || typeof fn !== 'function') return;
        if (!(SOLC_PROCESS_EVENTS as readonly string[]).includes(event)) return;
        const list = added.get(event) ?? [];
        list.push(fn as ProcessListenerFn);
        added.set(event, list);
    };

    for (const name of PROCESS_ON_METHODS) {
        const current = p[name];
        if (typeof current !== 'function') continue;
        const orig = current as (...args: unknown[]) => unknown;
        originals.set(name, { fn: orig, owned: Object.prototype.hasOwnProperty.call(p, name) });
        p[name] = (event: unknown, fn: unknown, ...rest: unknown[]) => {
            track(event, fn);
            return orig.call(p, event, fn, ...rest);
        };
    }

    let finished = false;
    return {
        finish: () => {
            if (!finished) {
                finished = true;
                for (const [name, saved] of originals) {
                    if (saved.owned) p[name] = saved.fn;
                    else delete p[name];
                }
            }
            return added;
        },
    };
}

/**
 * Remove only the tracked listener references (not later host handlers).
 *
 * @param added - Listeners captured during init and the first `compile()`
 */
function removeTrackedListeners(added: ProcessListenerMap): void {
    const p = nodeProcess();
    if (!p) return;
    for (const [event, fns] of added) {
        for (const fn of fns) p.removeListener(event, fn);
    }
    added.clear();
}

/**
 * Unlink a compiled soljson CJS module from Node's module graph so GC can
 * collect it once the wrapper drops its last reference (same idea as
 * `solc.loadRemoteVersion`).
 *
 * @param compiled - Module instance produced by `Module._compile`
 * @param filename - Absolute or virtual filename used as the cache key
 * @param ModuleCtor - Node `Module` constructor (for `_cache`)
 */
function detachCompiledNodeModule(
    compiled: NodeModuleInstance,
    filename: string,
    ModuleCtor: { _cache?: Record<string, unknown> },
): void {
    const parent = compiled.parent;
    if (parent?.children) {
        const index = parent.children.indexOf(compiled);
        if (index >= 0) parent.children.splice(index, 1);
    }
    try {
        if (ModuleCtor._cache && filename in ModuleCtor._cache) {
            delete ModuleCtor._cache[filename];
        }
    } catch { /* Module cache shape varies by Node version */ }
}

/**
 * Evaluate a soljson JS blob as a CommonJS module and return a release hook
 * that drops Emscripten `process` listeners (those closures pin the wasm heap).
 * Test-only.
 *
 * @param source - soljson JavaScript source
 * @param filename - Filename passed to `Module._compile`
 * @return Compiled `module.exports` plus a `release` function
 */
export async function compileSoljsonSourceForTests(
    source: string,
    filename: string,
): Promise<{ exports: unknown; release: () => void }> {
    const nodeModule = await import('node:module') as unknown as NodeModuleNamespace;
    const loaded = compileSoljsonSource(source, filename, nodeModule);
    return { exports: loaded.exports, release: loaded.release };
}

/**
 * Compile a soljson source string as a CommonJS module.
 *
 * @param source - JavaScript source of a soljson binary
 * @param filename - Virtual or disk path used as the module id
 * @param nodeModule - The `node:module` namespace
 * @param intercept - Optional open interceptor so the caller can cover setup/compile
 * @return Exports plus a `release` hook that drops Emscripten process listeners
 */
function compileSoljsonSource(
    source: string,
    filename: string,
    nodeModule: NodeModuleNamespace,
    intercept?: { finish: () => ProcessListenerMap },
): { exports: unknown; release: () => void } {
    const ModuleCtor = nodeModule.Module;
    const owned = intercept ?? interceptProcessListeners();
    const compiled = new ModuleCtor(filename);
    try {
        compiled._compile(source, filename);
    } catch (err) {
        removeTrackedListeners(owned.finish());
        detachCompiledNodeModule(compiled, filename, ModuleCtor);
        throw err;
    }
    detachCompiledNodeModule(compiled, filename, ModuleCtor);
    const addedListeners = intercept ? new Map() : owned.finish();
    return {
        exports: compiled.exports,
        release: () => {
            removeTrackedListeners(addedListeners);
            compiled.exports = {};
        },
    };
}

/**
 * Instantiate soljson in this process. Prefer the worker path on Node.
 *
 * @param source - soljson JavaScript source
 * @param filename - Module id passed to `_compile`
 * @return Compiler plus a release hook for Emscripten process listeners
 */
async function instantiateSoljson(source: string, filename: string): Promise<CachedCompiler> {
    const nodeModule = await import('node:module') as unknown as NodeModuleNamespace;
    const solc = await import('solc');
    const intercept = interceptProcessListeners();
    const loaded = compileSoljsonSource(source, filename, nodeModule, intercept);
    try {
        const compiler = solc.default.setupMethods(loaded.exports) as SolcInstance;
        try { compiler.version(); } catch { /* some binaries expose version only after compile */ }
        const added = intercept.finish();
        const origCompile = compiler.compile.bind(compiler);
        let firstCompile = true;
        compiler.compile = (input: string) => {
            if (!firstCompile) return origCompile(input);
            firstCompile = false;
            const duringCompile = interceptProcessListeners();
            try {
                return origCompile(input);
            } finally {
                const extra = duringCompile.finish();
                for (const [event, fns] of extra) {
                    const list = added.get(event) ?? [];
                    list.push(...fns);
                    added.set(event, list);
                }
            }
        };
        return {
            compiler,
            release: () => {
                removeTrackedListeners(added);
                loaded.release();
            },
        };
    } catch (err) {
        removeTrackedListeners(intercept.finish());
        loaded.release();
        throw err;
    }
}

/**
 * True when remote solc should run in a worker (reclaimable heap).
 * Set `C4_SOLC_IN_PROCESS=1` to force the in-process fallback.
 *
 * @return Whether this load should use a worker
 */
function canUseSolcWorker(): boolean {
    if (!isNodeEnvironment()) return false;
    if (process.env.C4_SOLC_IN_PROCESS === '1') return false;
    return true;
}

/**
 * Absolute path of the Node compiler worker next to this module.
 *
 * Resolve via `path.join` + `fileURLToPath`. A relative worker URL passed to
 * `new Worker` is rewritten by Vite as a browser Worker and then fails on
 * `node:worker_threads` (playground Docker/CI build).
 *
 * @return Filesystem path to `compiler-worker.js`
 */
async function resolveCompilerWorkerPath(): Promise<string> {
    const { fileURLToPath } = await import('node:url');
    const { dirname, join } = await import('node:path');
    return join(dirname(fileURLToPath(import.meta.url)), 'compiler-worker.js');
}

/**
 * Spawn a worker that owns one soljson instance.
 *
 * @param versionStr - Version string including leading `v`
 * @param diskPath - Cached soljson path, if written
 * @param source - In-memory soljson when no disk path is available
 * @return Compiler proxy whose `compileAsync` posts to the worker
 */
async function createWorkerCompiler(
    versionStr: string,
    diskPath: string | null,
    source: string | null,
): Promise<CachedCompiler> {
    explainerLog('info', 'spawning compiler worker', { scope: 'solc', version: versionStr });
    const spawnStarted = Date.now();
    const { Worker } = await import('node:worker_threads');
    const worker = new Worker(await resolveCompilerWorkerPath(), {
        workerData: {
            diskPath: diskPath || undefined,
            source: diskPath ? undefined : source,
            filename: diskPath || `soljson-${versionStr}.js`,
        },
    });

    const pending = new Map<number, { resolve: (out: string) => void; reject: (err: Error) => void }>();
    let nextId = 1;

    const failAll = (err: Error): void => {
        for (const wait of pending.values()) wait.reject(err);
        pending.clear();
    };

    const handle = await new Promise<CachedCompiler>((resolve, reject) => {
        let settled = false;
        worker.on('message', (msg: { type: string; id?: number; ok?: boolean; output?: string; error?: string }) => {
            if (msg.type === 'ready' && !settled) {
                settled = true;
                explainerLog('info', 'compiler worker ready', {
                    scope: 'solc', version: versionStr, ms: elapsedMs(spawnStarted),
                });
                resolve({
                    compiler: {
                        compile: () => {
                            throw new Error('worker compiler requires compileAsync');
                        },
                        compileAsync: (input: string) => new Promise<string>((res, rej) => {
                            const id = nextId++;
                            pending.set(id, { resolve: res, reject: rej });
                            worker.postMessage({ id, input });
                        }),
                        version: () => versionStr.startsWith('v') ? versionStr.slice(1) : versionStr,
                    },
                    release: () => {
                        failAll(new Error('compiler worker released'));
                        void worker.terminate();
                    },
                });
                return;
            }
            if (msg.type === 'error' && !settled) {
                settled = true;
                void worker.terminate();
                reject(new Error(msg.error || 'compiler worker failed'));
                return;
            }
            if (msg.type === 'result' && msg.id != null) {
                const wait = pending.get(msg.id);
                if (!wait) return;
                pending.delete(msg.id);
                if (msg.ok && msg.output !== undefined) wait.resolve(msg.output);
                else wait.reject(new Error(msg.error || 'compile failed'));
            }
        });
        worker.on('error', (err) => {
            const error = err instanceof Error ? err : new Error(String(err));
            if (!settled) {
                settled = true;
                reject(error);
            }
            failAll(error);
        });
        worker.on('exit', (code) => {
            const error = new Error(`compiler worker exited (${code})`);
            if (!settled) {
                settled = true;
                reject(error);
            }
            failAll(error);
        });
    });

    return handle;
}

/**
 * Load a remote solc version from disk or soliditylang.org.
 *
 * @param versionStr - Version string including leading `v`
 * @return Worker-backed compiler (default) or in-process fallback
 */
async function loadRemoteCompilerNode(versionStr: string): Promise<CachedCompiler> {
    const diskPath = await resolveSolcDiskPath(versionStr);
    const fs = await import('fs');

    if (canUseSolcWorker() && diskPath && fs.existsSync(diskPath)) {
        try {
            return await createWorkerCompiler(versionStr, diskPath, null);
        } catch {
            /* corrupt cache -- re-download below */
        }
    }

    let source: string | null = null;
    if (diskPath) {
        try {
            source = fs.readFileSync(diskPath, 'utf-8');
        } catch {
            source = null;
        }
    }

    if (!source) {
        source = await downloadSoljson(versionStr);
        if (diskPath) {
            try {
                const pathMod = await import('path');
                fs.mkdirSync(pathMod.dirname(diskPath), { recursive: true });
                fs.writeFileSync(diskPath, source, 'utf-8');
            } catch { /* disk full / permissions -- still use the in-memory source */ }
        }
    }

    try {
        if (canUseSolcWorker()) {
            return await createWorkerCompiler(versionStr, diskPath, source);
        }
        return await instantiateSoljson(source, diskPath || `soljson-${versionStr}.js`);
    } catch (err) {
        const message = err instanceof Error ? err.message : String(err);
        throw new Error(`Failed to load solc ${versionStr}: ${message}`);
    }
}

async function downloadSoljson(versionStr: string): Promise<string> {
    const file = `soljson-${versionStr}.js`;
    const wasmResponse = await fetch(`${SOLC_WASM_BASE}/${file}`);
    if (wasmResponse.ok) return readSoljsonBody(wasmResponse, versionStr);

    const binResponse = await fetch(`${SOLC_BIN_BASE}/${file}`);
    if (binResponse.ok) return readSoljsonBody(binResponse, versionStr);

    throw new Error(`Failed to load solc ${versionStr}: HTTP ${binResponse.status}`);
}

/**
 * Read a soljson response, rejecting bodies above `MAX_SOLJSON_CHARS`.
 *
 * `Content-Length` is checked first. The body is then read in chunks so an
 * oversized or chunked response is cancelled before it is buffered whole.
 * soljson is ASCII, so the byte cap and the character cap match.
 *
 * @param response - Successful fetch of a soljson binary
 * @param versionStr - Version used in the error message
 * @return JavaScript source of the compiler
 */
async function readSoljsonBody(response: Response, versionStr: string): Promise<string> {
    const tooLarge = `Failed to load solc ${versionStr}: binary exceeds ${MAX_SOLJSON_CHARS} chars`;
    const declared = Number(response.headers.get('content-length'));
    if (Number.isFinite(declared) && declared > MAX_SOLJSON_CHARS) {
        await response.body?.cancel();
        throw new Error(tooLarge);
    }
    const reader = response.body?.getReader();
    if (!reader) {
        const source = await response.text();
        if (source.length > MAX_SOLJSON_CHARS) throw new Error(tooLarge);
        return source;
    }
    const decoder = new TextDecoder();
    let source = '';
    let total = 0;
    try {
        while (true) {
            const { done, value } = await reader.read();
            if (done) break;
            total += value.byteLength;
            if (total > MAX_SOLJSON_CHARS) {
                await reader.cancel();
                throw new Error(tooLarge);
            }
            source += decoder.decode(value, { stream: true });
        }
    } catch (err) {
        await reader.cancel().catch(() => { /* already cancelled or closed */ });
        throw err;
    }
    source += decoder.decode();
    if (source.length > MAX_SOLJSON_CHARS) throw new Error(tooLarge);
    return source;
}

/**
 * Resolve `soljson-vX.Y.Z+commit….js` for an npm-style release (`0.8.36`).
 * Prefers the wasm list, then the asm.js list. The filename must be
 * `soljson-v{release}+commit….js` on those hosts; a list entry for another
 * compiler version is rejected.
 *
 * @param release - `major.minor.patch` release, no commit suffix
 * @return Filename such as `soljson-v0.8.36+commit.8a079791.js`
 */
async function soljsonFileForRelease(release: string): Promise<string> {
    if (!/^\d+\.\d+\.\d+$/.test(release)) {
        throw new Error(`Invalid solc release: ${release}`);
    }
    for (const base of [SOLC_WASM_BASE, SOLC_BIN_BASE]) {
        const response = await fetch(`${base}/list.json`);
        if (!response.ok) continue;
        const list = await response.json() as { releases?: Record<string, unknown> };
        const fileName = list.releases?.[release];
        const releasePrefix = `soljson-v${release}+commit.`;
        if (
            typeof fileName === 'string'
            && fileName.startsWith(releasePrefix)
            && SOLJSON_FILE_RE.test(fileName)
        ) return fileName;
    }
    throw new Error(`No soljson build listed for ${release}`);
}

/**
 * Full `vX.Y.Z+commit…` version of `BUNDLED_SOLC_RELEASE` from the official list.
 *
 * @return Version string including the leading `v`
 */
async function bundledWasmVersion(): Promise<string> {
    const fileName = await soljsonFileForRelease(BUNDLED_SOLC_RELEASE);
    const version = fileName.slice('soljson-'.length, -'.js'.length);
    validateCompilerVersion(version);
    return version.startsWith('v') ? version : `v${version}`;
}

/**
 * Download the wasm (or bin) build of the npm `solc` release and start it
 * in a browser worker.
 *
 * @return Compiler whose `compileAsync` posts standard JSON to that worker
 */
async function loadBundledCompilerBrowser(): Promise<CachedCompiler> {
    const versionStr = await bundledWasmVersion();
    explainerLog('info', 'loading bundled compiler for browser', { scope: 'solc', version: versionStr });
    const source = await downloadSoljson(versionStr);
    return createBrowserCompiler(source, versionStr);
}

/**
 * Classic worker source. Loads soljson via `importScripts` so `var Module`
 * becomes the worker global and does not replace the page's Emscripten `Module`
 * (the Colibri wasm bundle).
 *
 * Compilation entry points changed across solc releases. We detect the
 * exported C symbols at init time and bind the right wrapper:
 *
 * - `solidity_compile` (0.5.9+) with 5-arg callback `(context, kind, data, contents, error)`.
 * - `compileStandard` (0.4.11 - 0.5.8) with 1-arg callback `(path) → json_ptr`.
 *   The returned pointer is a C string containing JSON like
 *   `{"contents": "..."}` or `{"error": "..."}`.
 *
 * Both callbacks only *report failure*: skeleton layout and Sourcify standard
 * JSON both inline every source, so a missing file is an error either way.
 * `solidity_alloc` is used when exported, else we fall back to `malloc`
 * (every Emscripten binary ships it).
 */
export const BROWSER_SOLC_WORKER = `
var compileBound = null;
self.onmessage = function (event) {
    var msg = event.data;
    if (!msg || typeof msg !== 'object') return;
    if (msg.type === 'init') {
        try {
            var version = initSoljson(msg.source);
            self.postMessage({ type: 'ready', version: version });
        } catch (err) {
            self.postMessage({ type: 'error', error: err && err.message ? err.message : String(err) });
        }
        return;
    }
    if (msg.type === 'compile') {
        try {
            if (!compileBound) throw new Error('compiler is not initialized');
            self.postMessage({ type: 'result', id: msg.id, ok: true, output: compileBound(msg.input) });
        } catch (err) {
            self.postMessage({ type: 'result', id: msg.id, ok: false, error: err && err.message ? err.message : String(err) });
        }
    }
};
function initSoljson(source) {
    if (typeof source !== 'string' || source.length < 1000 || source.length > ${MAX_SOLJSON_CHARS}) {
        throw new Error('invalid soljson source');
    }
    var blob = new Blob([source], { type: 'text/javascript' });
    var url = URL.createObjectURL(blob);
    try {
        importScripts(url);
    } finally {
        URL.revokeObjectURL(url);
    }
    var Module = self.Module;
    if (!Module || typeof Module.cwrap !== 'function' || typeof Module.addFunction !== 'function') {
        throw new Error('soljson did not initialize');
    }

    // Version string: new builds expose solidity_version, old builds expose version.
    // Old emscripten cwrap returns a wrapper that only aborts when invoked, so
    // guard with the raw export lookup before calling anything.
    var version = 'unknown';
    if (Module['_solidity_version']) {
        version = Module.cwrap('solidity_version', 'string', [])();
    } else if (Module['_version']) {
        version = Module.cwrap('version', 'string', [])();
    }

    var alloc = Module['_solidity_alloc']
        ? Module.cwrap('solidity_alloc', 'number', ['number'])
        : Module.cwrap('malloc', 'number', ['number']);

    if (Module['_solidity_compile']) {
        // 0.5.9+ API: 3-arg entry point with modern 5-arg import callback.
        var compile = Module.cwrap('solidity_compile', 'string', ['string', 'number', 'number']);
        var reset = Module['_solidity_reset'] ? Module.cwrap('solidity_reset', null, []) : null;
        compileBound = function (input) {
            var cb = Module.addFunction(function (context, kind, data, contents, error) {
                var kindStr = Module.UTF8ToString(kind);
                var message = kindStr === 'smt-query'
                    ? 'SMT solver callback not supported'
                    : 'File import callback not supported';
                var length = Module.lengthBytesUTF8(message);
                var buffer = alloc(length + 1);
                Module.stringToUTF8(message, buffer, length + 1);
                Module.setValue(error, buffer, '*');
            }, 'viiiii');
            try {
                return compile(input, cb, 0);
            } finally {
                Module.removeFunction(cb);
                if (reset) reset();
            }
        };
    } else if (Module['_compileStandard']) {
        // 0.4.11 - 0.5.8 API: compileStandard with 1-arg callback that returns
        // a C-string pointer to a JSON object. We only ever report an error.
        var compileStandard = Module.cwrap('compileStandard', 'string', ['string', 'number']);
        var errorBlob = '{"error":"File import callback not supported"}';
        compileBound = function (input) {
            var cb = Module.addFunction(function (path) {
                var length = Module.lengthBytesUTF8(errorBlob);
                var buffer = alloc(length + 1);
                Module.stringToUTF8(errorBlob, buffer, length + 1);
                return buffer;
            }, 'ii');
            try {
                return compileStandard(input, cb);
            } finally {
                Module.removeFunction(cb);
            }
        };
    } else {
        throw new Error('solc ' + version + ' is too old for in-browser verification (needs >= 0.4.11)');
    }

    return version;
}
`;

/**
 * Run a soljson binary in a browser worker.
 *
 * `compile()` throws: callers must use `compileAsync`, same as the Node worker.
 *
 * @param source - soljson JavaScript source fetched from soliditylang.org
 * @param versionStr - Version string including the leading `v`, for logs
 * @return Compiler proxy plus a `release` hook that terminates the worker
 */
function createBrowserCompiler(source: string, versionStr: string): Promise<CachedCompiler> {
    if (typeof Worker === 'undefined') {
        return Promise.reject(new Error(`Failed to load solc ${versionStr}: Worker is not available`));
    }
    const workerUrl = URL.createObjectURL(new Blob([BROWSER_SOLC_WORKER], { type: 'text/javascript' }));
    const worker = new Worker(workerUrl);
    const pending = new Map<number, { resolve: (out: string) => void; reject: (err: Error) => void }>();
    let nextId = 1;

    const failAll = (err: Error): void => {
        for (const wait of pending.values()) wait.reject(err);
        pending.clear();
    };

    return new Promise<CachedCompiler>((resolve, reject) => {
        let settled = false;
        const fail = (err: Error): void => {
            if (!settled) {
                settled = true;
                reject(err);
            }
            failAll(err);
            worker.terminate();
            URL.revokeObjectURL(workerUrl);
        };

        worker.onmessage = (event: MessageEvent) => {
            const data = event.data;
            if (!data || typeof data !== 'object') return;
            const msg = data as { type?: string; id?: number; ok?: boolean; output?: string; error?: string; version?: string };
            if (msg.type === 'ready' && !settled) {
                settled = true;
                URL.revokeObjectURL(workerUrl);
                const version = typeof msg.version === 'string' && msg.version.length > 0
                    ? msg.version
                    : (versionStr.startsWith('v') ? versionStr.slice(1) : versionStr);
                explainerLog('info', 'browser compiler ready', { scope: 'solc', version });
                resolve({
                    compiler: {
                        compile: () => {
                            throw new Error('browser compiler requires compileAsync');
                        },
                        compileAsync: (input: string) => new Promise<string>((res, rej) => {
                            const id = nextId++;
                            pending.set(id, { resolve: res, reject: rej });
                            worker.postMessage({ type: 'compile', id, input });
                        }),
                        version: () => version,
                    },
                    release: () => {
                        failAll(new Error('browser compiler released'));
                        worker.terminate();
                    },
                });
                return;
            }
            if (msg.type === 'error' && !settled) {
                fail(new Error(msg.error || `Failed to load solc ${versionStr}`));
                return;
            }
            if (msg.type === 'result' && msg.id != null) {
                const wait = pending.get(msg.id);
                if (!wait) return;
                pending.delete(msg.id);
                if (msg.ok && typeof msg.output === 'string') wait.resolve(msg.output);
                else wait.reject(new Error(msg.error || 'compile failed'));
            }
        };
        worker.onerror = (event) => {
            fail(new Error(event.message || `Failed to load solc ${versionStr}`));
        };
        worker.postMessage({ type: 'init', source });
    });
}

/**
 * Load a remote solc version in the browser.
 *
 * The npm package's `loadRemoteVersion` uses Node's `https` and `Module._compile`,
 * which the Vite bundle cannot run. The binary is fetched (wasm, then bin) and
 * started in a worker instead.
 *
 * @param versionStr - Version string including the leading `v`
 * @return Worker-backed compiler
 */
async function loadRemoteCompilerBrowser(versionStr: string): Promise<CachedCompiler> {
    explainerLog('info', 'loading remote compiler for browser', { scope: 'solc', version: versionStr });
    const source = await downloadSoljson(versionStr);
    return createBrowserCompiler(source, versionStr);
}

/**
 * Compile Solidity source via stdJsonInput with the given compiler version and
 * verify that the produced runtime bytecode matches the expected `codeHash`.
 *
 * Only used for bytecode verification -- storageLayout comes from the skeleton
 * pipeline in `layout.ts`.
 *
 * @param stdJsonInput - Solidity standard JSON input (from Sourcify)
 * @param compilerVersion - Exact compiler version string
 * @param expectedCodeHash - `keccak256` of the on-chain deployed runtime bytecode
 * @param sources - Original source files for passthrough
 * @return Verification result with ABI on success
 */
export async function compileAndVerify(
    stdJsonInput: Record<string, unknown>,
    compilerVersion: string,
    expectedCodeHash: string,
    sources: Record<string, { content: string }>,
    options?: CompileOptions,
): Promise<CompileResult> {
    let compiler: SolcInstance;
    try {
        compiler = await loadCompiler(compilerVersion);
    } catch (err) {
        explainerLog('warn', 'failed to load compiler', {
            scope: 'solc',
            version: compilerVersion,
            error: err instanceof Error ? err.message : String(err),
        });
        return { verified: false, abi: null, sources: null };
    }

    const wantSourceMap = options?.includeSourceMap === true;
    const input = { ...stdJsonInput } as Record<string, unknown>;
    const settings = { ...(input.settings as Record<string, unknown> || {}) };
    const outputSelection = { ...(settings.outputSelection as Record<string, Record<string, string[]>> || {}) };

    const ensure = (existing: string[]): string[] => {
        if (!existing.includes('abi')) existing.push('abi');
        if (!existing.includes('evm.deployedBytecode.object')) existing.push('evm.deployedBytecode.object');
        if (wantSourceMap && !existing.includes('evm.deployedBytecode.sourceMap')) {
            existing.push('evm.deployedBytecode.sourceMap');
        }
        return existing;
    };

    for (const file of Object.keys(outputSelection)) {
        for (const contract of Object.keys(outputSelection[file])) {
            outputSelection[file][contract] = ensure(outputSelection[file][contract] || []);
        }
    }

    if (!Object.keys(outputSelection).length) {
        const base = ['abi', 'evm.deployedBytecode.object'];
        if (wantSourceMap) base.push('evm.deployedBytecode.sourceMap');
        outputSelection['*'] = { '*': base };
    }

    settings.outputSelection = outputSelection;
    input.settings = settings;

    const files = stdJsonInput.sources && typeof stdJsonInput.sources === 'object'
        ? Object.keys(stdJsonInput.sources as object).length
        : 0;
    explainerLog('debug', 'compile start', { scope: 'solc', version: compilerVersion, files });
    const compileStarted = Date.now();

    let output: Record<string, unknown>;
    try {
        const raw = compiler.compileAsync
            ? await compiler.compileAsync(JSON.stringify(input))
            : compiler.compile(JSON.stringify(input));
        output = JSON.parse(raw);
    } catch (err) {
        explainerLog('warn', 'compile failed', {
            scope: 'solc',
            version: compilerVersion,
            ms: elapsedMs(compileStarted),
            error: err instanceof Error ? err.message : String(err),
        });
        return { verified: false, abi: null, sources: null };
    }
    explainerLog('info', 'compile done', {
        scope: 'solc', version: compilerVersion, ms: elapsedMs(compileStarted), files,
    });

    const contracts = output.contracts as Record<string, Record<string, Record<string, unknown>>> | undefined;
    if (!contracts) return { verified: false, abi: null, sources: null };

    const sourceIndex = wantSourceMap ? buildSourceIndex(output) : null;

    const normalizedHash = expectedCodeHash.toLowerCase();
    const partialCandidates: Array<{ contractData: Record<string, unknown>; bytecodeHex: string }> = [];

    for (const file of Object.values(contracts)) {
        for (const contractData of Object.values(file)) {
            const evm = contractData.evm as Record<string, Record<string, string>> | undefined;
            const bytecodeHex = evm?.deployedBytecode?.object;
            if (!bytecodeHex || bytecodeHex.length < 2) continue;

            const hash = hashRuntimeBytecode(bytecodeHex);
            if (!hash) continue;
            if (hash.toLowerCase() === normalizedHash) {
                return buildVerifiedResult(contractData, bytecodeHex, wantSourceMap, sourceIndex, sources);
            }
            // Keep this contract for the partial-match fallback below.
            partialCandidates.push({ contractData, bytecodeHex });
        }
    }

    // Full match failed. Try a Sourcify-style partial match if the caller
    // supplied the on-chain bytecode: strip the CBOR metadata trailer from
    // both sides and compare the remaining code.
    const fetchOnChain = options?.fetchOnChainBytecode;
    if (fetchOnChain && partialCandidates.length > 0) {
        let onChainHex: string | null = null;
        try {
            onChainHex = await fetchOnChain();
        } catch (err) {
            explainerLog('warn', 'fetchOnChainBytecode threw', {
                scope: 'solc', error: err instanceof Error ? err.message : String(err),
            });
        }
        if (onChainHex && HEX_BYTECODE_RE.test(onChainHex)) {
            // Trust-but-verify: only accept the fetched bytecode when its
            // keccak matches the expected hash from the verified accessList.
            const onChainHash = hashRuntimeBytecode(onChainHex);
            if (onChainHash && onChainHash.toLowerCase() === normalizedHash) {
                const onChainStripped = stripCborMetadata(onChainHex);
                if (onChainStripped) {
                    const expectedPartial = hashRuntimeBytecode(onChainStripped);
                    for (const { contractData, bytecodeHex } of partialCandidates) {
                        const stripped = stripCborMetadata(bytecodeHex);
                        if (!stripped) continue;
                        const candidate = hashRuntimeBytecode(stripped);
                        if (candidate && expectedPartial && candidate === expectedPartial) {
                            explainerLog('info', 'bytecode partial match', {
                                scope: 'solc', version: compilerVersion,
                            });
                            return buildVerifiedResult(
                                contractData, bytecodeHex, wantSourceMap, sourceIndex, sources,
                            );
                        }
                    }
                }
            } else {
                explainerLog('warn', 'on-chain bytecode hash mismatch', {
                    scope: 'solc', expected: normalizedHash, got: onChainHash ?? '<null>',
                });
            }
        }
    }

    return { verified: false, abi: null, sources: null };
}

/**
 * Build a successful `CompileResult` for a verified contract.
 *
 * Extracted so the full-match and partial-match code paths in
 * `compileAndVerify` can share the same assembly logic (ABI extraction and
 * optional source-map packaging).
 *
 * @param contractData - Raw contract entry from solc output
 * @param bytecodeHex - Deployed runtime bytecode from the compiled contract
 * @param wantSourceMap - `true` when the caller asked for the source-map payload
 * @param sourceIndex - `sourceId → filename` table from the solc output (may be `null`)
 * @param sources - Original sources to pass through to the caller
 * @return Verified compile result including ABI (and optionally the source-map bundle)
 */
function buildVerifiedResult(
    contractData: Record<string, unknown>,
    bytecodeHex: string,
    wantSourceMap: boolean,
    sourceIndex: Map<number, string> | null,
    sources: Record<string, { content: string }>,
): CompileResult {
    const abi = Array.isArray(contractData.abi) ? contractData.abi : null;
    let sourceMap: CompileResult['sourceMap'] = null;
    if (wantSourceMap && sourceIndex) {
        const evm = contractData.evm as Record<string, Record<string, string>> | undefined;
        const map = typeof evm?.deployedBytecode?.sourceMap === 'string'
            ? evm.deployedBytecode.sourceMap
            : '';
        if (map) {
            sourceMap = {
                sourceMap: map,
                runtimeBytecode: normalizeBytecode(bytecodeHex),
                sourceIndex,
            };
        }
    }
    return { verified: true, abi, sources, sourceMap };
}

/**
 * Build the `sourceId → filename` table from a solc standard-json output.
 *
 * The compiler numbers sources sequentially. The number is what appears in
 * source-map entries (`s:l:f:...`).
 *
 * @param output - Parsed solc output
 * @return Map from source id to filename (may be empty on malformed output)
 */
function buildSourceIndex(output: Record<string, unknown>): Map<number, string> {
    const result = new Map<number, string>();
    const sources = output.sources as Record<string, Record<string, unknown>> | undefined;
    if (!sources) return result;
    for (const [name, meta] of Object.entries(sources)) {
        const id = meta?.id;
        if (typeof id === 'number' && Number.isFinite(id) && id >= 0) {
            result.set(id, name);
        }
    }
    return result;
}

/**
 * Ensure the bytecode is a `0x`-prefixed lowercase hex string.
 *
 * @param bytecodeHex - Runtime bytecode as returned by solc
 * @return Normalized `0x...` form
 */
function normalizeBytecode(bytecodeHex: string): string {
    const trimmed = bytecodeHex.startsWith('0x') || bytecodeHex.startsWith('0X')
        ? bytecodeHex.slice(2)
        : bytecodeHex;
    return '0x' + trimmed.toLowerCase();
}
