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
const compilerCache = new Map<string, SolcInstance>();
let bundledVersion: string | null = null;

const COMPILER_VERSION_RE = /^v?\d+\.\d+\.\d+\+commit\.[0-9a-f]{6,10}(\.Emscripten\.clang)?$/;

function extractShortVersion(fullVersion: string): string {
    const match = fullVersion.match(/^(\d+\.\d+\.\d+\+commit\.[a-f0-9]+)/);
    return match?.[1] ?? fullVersion;
}

function validateCompilerVersion(version: string): void {
    if (!COMPILER_VERSION_RE.test(version)) {
        throw new Error(`Invalid compiler version format: ${version}`);
    }
}

function evictOldestCompiler(): void {
    if (compilerCache.size >= MAX_CACHED_COMPILERS) {
        const oldest = compilerCache.keys().next().value;
        if (oldest) compilerCache.delete(oldest);
    }
}

/**
 * Get the bundled solc compiler (shipped with the `solc` npm package).
 */
export async function getBundledCompiler(): Promise<SolcInstance> {
    const solc = await import('solc');
    const compiler = solc.default as unknown as SolcInstance;
    bundledVersion = extractShortVersion(compiler.version());
    return compiler;
}

/**
 * Load a specific solc compiler version. If the version matches the bundled
 * compiler, returns it directly; otherwise downloads via `loadRemoteVersion`.
 * Caches loaded WASM binaries in memory (~8 MB per version).
 *
 * @param version - Compiler version string, e.g. `"0.8.19+commit.7dd6d404"` or `"v0.8.19+commit.7dd6d404"`
 * @return Loaded compiler instance
 */
export async function loadCompiler(version: string): Promise<SolcInstance> {
    validateCompilerVersion(version);

    const normalizedVersion = version.startsWith('v') ? version.slice(1) : version;
    const shortVersion = extractShortVersion(normalizedVersion);

    const cached = compilerCache.get(shortVersion);
    if (cached) return cached;

    const bundled = await getBundledCompiler();
    if (shortVersion === bundledVersion) {
        compilerCache.set(shortVersion, bundled);
        return bundled;
    }

    const solc = await import('solc');
    const versionStr = version.startsWith('v') ? version : 'v' + version;
    const compiler = await new Promise<SolcInstance>((resolve, reject) => {
        solc.default.loadRemoteVersion(versionStr, (err: Error | null, instance: SolcInstance) => {
            if (err) reject(new Error(`Failed to load solc ${versionStr}: ${err.message}`));
            else resolve(instance);
        });
    });

    evictOldestCompiler();
    compilerCache.set(shortVersion, compiler);
    return compiler;
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

            const cleanHex = bytecodeHex.startsWith('0x') ? bytecodeHex : '0x' + bytecodeHex;
            const hash = keccak256(cleanHex);
            if (hash.toLowerCase() === normalizedHash) {
                const abi = Array.isArray(contractData.abi) ? contractData.abi : null;
                return { verified: true, abi, sources };
            }
        }
    }

    return { verified: false, abi: null, sources: null };
}
