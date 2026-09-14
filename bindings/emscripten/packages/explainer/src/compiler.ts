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

interface SolcInstance {
    compile(input: string): string;
    version(): string;
}

interface CompileResult {
    verified: boolean;
    abi: unknown[] | null;
    sources: Record<string, { content: string }> | null;
}

const MAX_CACHED_COMPILERS = 3;
const SOLC_WASM_BASE = 'https://binaries.soliditylang.org/wasm';
const SOLC_BIN_BASE = 'https://binaries.soliditylang.org/bin';

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

/** Drop the oldest LRU entry and release its Emscripten listeners / module refs. */
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
}

/**
 * Get the bundled solc compiler (shipped with the `solc` npm package).
 *
 * @return Bundled compiler instance
 */
export async function getBundledCompiler(): Promise<SolcInstance> {
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
 * Number of compiler versions currently retained in the in-memory LRU. Test-only.
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
 * Caches loaded compilers in memory (~8–50 MB wasm heap per version, LRU cap 3).
 * Evicted instances drop Emscripten `process` listeners so the heap can be GC'd.
 *
 * @param version - Compiler version string, e.g. `"0.8.19+commit.7dd6d404"` or `"v0.8.19+commit.7dd6d404"`
 * @return Loaded compiler instance
 */
export async function loadCompiler(version: string): Promise<SolcInstance> {
    validateCompilerVersion(version);

    const normalizedVersion = version.startsWith('v') ? version.slice(1) : version;
    const shortVersion = extractShortVersion(normalizedVersion);

    const cached = compilerCache.get(shortVersion);
    if (cached) return cached.compiler;

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

/**
 * Snapshot `process` listeners for events Emscripten soljson registers.
 *
 * @return Event name → listener functions present before `_compile`
 */
function snapshotSolcProcessListeners(): ProcessListenerMap {
    const snap: ProcessListenerMap = new Map();
    const p = nodeProcess();
    if (!p) return snap;
    for (const event of SOLC_PROCESS_EVENTS) {
        snap.set(event, p.listeners(event).slice());
    }
    return snap;
}

/**
 * Listeners that appeared after `before` was taken.
 *
 * @param before - Snapshot taken immediately before `_compile`
 * @return New listeners keyed by event name
 */
function listenersAddedSince(before: ProcessListenerMap): ProcessListenerMap {
    const added: ProcessListenerMap = new Map();
    const p = nodeProcess();
    if (!p) return added;
    for (const event of SOLC_PROCESS_EVENTS) {
        const prev = before.get(event) ?? [];
        const extra = p.listeners(event).filter(fn => !prev.includes(fn));
        if (extra.length) added.set(event, extra);
    }
    return added;
}

/**
 * Remove only the tracked listener references (not later host handlers).
 *
 * @param added - Listener set captured right after `_compile` / `setupMethods`
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
 * @return Exports plus a `release` hook that drops Emscripten process listeners
 */
function compileSoljsonSource(
    source: string,
    filename: string,
    nodeModule: NodeModuleNamespace,
): { exports: unknown; release: () => void } {
    const ModuleCtor = nodeModule.Module;
    const beforeListeners = snapshotSolcProcessListeners();
    const compiled = new ModuleCtor(filename);
    try {
        compiled._compile(source, filename);
    } catch (err) {
        removeTrackedListeners(listenersAddedSince(beforeListeners));
        detachCompiledNodeModule(compiled, filename, ModuleCtor);
        throw err;
    }
    const addedListeners = listenersAddedSince(beforeListeners);
    detachCompiledNodeModule(compiled, filename, ModuleCtor);
    return {
        exports: compiled.exports,
        release: () => {
            removeTrackedListeners(addedListeners);
            compiled.exports = {};
        },
    };
}

async function instantiateSoljson(source: string, filename: string): Promise<CachedCompiler> {
    const nodeModule = await import('node:module') as unknown as NodeModuleNamespace;
    const loaded = compileSoljsonSource(source, filename, nodeModule);
    const beforeSetup = snapshotSolcProcessListeners();
    try {
        const solc = await import('solc');
        const compiler = solc.default.setupMethods(loaded.exports) as SolcInstance;
        const addedDuringSetup = listenersAddedSince(beforeSetup);
        return {
            compiler,
            release: () => {
                removeTrackedListeners(addedDuringSetup);
                loaded.release();
            },
        };
    } catch (err) {
        removeTrackedListeners(listenersAddedSince(beforeSetup));
        loaded.release();
        throw err;
    }
}

async function loadRemoteCompilerNode(versionStr: string): Promise<CachedCompiler> {
    const diskPath = await resolveSolcDiskPath(versionStr);
    const fs = await import('fs');

    if (diskPath) {
        try {
            const source = fs.readFileSync(diskPath, 'utf-8');
            return await instantiateSoljson(source, diskPath);
        } catch {
            /* missing or corrupt -- fall through to download */
        }
    }

    const source = await downloadSoljson(versionStr);

    if (diskPath) {
        try {
            const pathMod = await import('path');
            fs.mkdirSync(pathMod.dirname(diskPath), { recursive: true });
            fs.writeFileSync(diskPath, source, 'utf-8');
        } catch { /* disk full / permissions -- still use the in-memory source */ }
    }

    try {
        return await instantiateSoljson(source, `soljson-${versionStr}.js`);
    } catch (err) {
        const message = err instanceof Error ? err.message : String(err);
        throw new Error(`Failed to load solc ${versionStr}: ${message}`);
    }
}

async function downloadSoljson(versionStr: string): Promise<string> {
    const file = `soljson-${versionStr}.js`;
    const wasmResponse = await fetch(`${SOLC_WASM_BASE}/${file}`);
    if (wasmResponse.ok) return wasmResponse.text();

    const binResponse = await fetch(`${SOLC_BIN_BASE}/${file}`);
    if (binResponse.ok) return binResponse.text();

    throw new Error(`Failed to load solc ${versionStr}: HTTP ${binResponse.status}`);
}

async function loadRemoteCompilerBrowser(versionStr: string): Promise<CachedCompiler> {
    const solc = await import('solc');
    const compiler = await new Promise<SolcInstance>((resolve, reject) => {
        solc.default.loadRemoteVersion(versionStr, (err: Error | null, instance: SolcInstance) => {
            if (err) reject(new Error(`Failed to load solc ${versionStr}: ${err.message}`));
            else resolve(instance);
        });
    });
    return { compiler, release: () => { /* browser: no process-listener leak */ } };
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
): Promise<CompileResult> {
    let compiler: SolcInstance;
    try {
        compiler = await loadCompiler(compilerVersion);
    } catch {
        return { verified: false, abi: null, sources: null };
    }

    const input = { ...stdJsonInput } as Record<string, unknown>;
    const settings = { ...(input.settings as Record<string, unknown> || {}) };
    const outputSelection = { ...(settings.outputSelection as Record<string, Record<string, string[]>> || {}) };

    for (const file of Object.keys(outputSelection)) {
        for (const contract of Object.keys(outputSelection[file])) {
            const existing = outputSelection[file][contract] || [];
            if (!existing.includes('abi')) existing.push('abi');
            if (!existing.includes('evm.deployedBytecode.object')) existing.push('evm.deployedBytecode.object');
            outputSelection[file][contract] = existing;
        }
    }

    if (!Object.keys(outputSelection).length) {
        outputSelection['*'] = { '*': ['abi', 'evm.deployedBytecode.object'] };
    }

    settings.outputSelection = outputSelection;
    input.settings = settings;

    let output: Record<string, unknown>;
    try {
        output = JSON.parse(compiler.compile(JSON.stringify(input)));
    } catch {
        return { verified: false, abi: null, sources: null };
    }

    const contracts = output.contracts as Record<string, Record<string, Record<string, unknown>>> | undefined;
    if (!contracts) return { verified: false, abi: null, sources: null };

    const normalizedHash = expectedCodeHash.toLowerCase();
    for (const file of Object.values(contracts)) {
        for (const contractData of Object.values(file)) {
            const evm = contractData.evm as Record<string, Record<string, string>> | undefined;
            const bytecodeHex = evm?.deployedBytecode?.object;
            if (!bytecodeHex || bytecodeHex.length < 2) continue;

            const hash = hashRuntimeBytecode(bytecodeHex);
            if (!hash) continue;
            if (hash.toLowerCase() === normalizedHash) {
                const abi = Array.isArray(contractData.abi) ? contractData.abi : null;
                return { verified: true, abi, sources };
            }
        }
    }

    return { verified: false, abi: null, sources: null };
}
