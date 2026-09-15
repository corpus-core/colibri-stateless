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
 * Worker thread that owns a single soljson instance. Terminating the worker
 * returns the Emscripten heap to the OS -- in-process LRU cannot do that.
 */
import { parentPort, workerData } from 'node:worker_threads';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { Module } from 'node:module';

interface WorkerInit {
    diskPath?: string;
    source?: string;
    filename: string;
}

interface CompileRequest {
    id: number;
    input: string;
}

/**
 * Load soljson in this thread and serve compile requests until terminated.
 */
function main(): void {
    if (!parentPort) {
        throw new Error('compiler-worker must run as a worker thread');
    }

    const init = workerData as WorkerInit;
    const require = createRequire(import.meta.url);

    try {
        const js = init.diskPath ? readFileSync(init.diskPath, 'utf-8') : init.source;
        if (!js) throw new Error('missing soljson source');

        const compiled = new Module(init.filename) as unknown as { _compile(source: string, filename: string): void; exports: unknown };
        compiled._compile(js, init.filename);

        const solcMod = require('solc') as {
            setupMethods?: (exports: unknown) => { compile(input: string): string; version(): string };
            default?: { setupMethods?: (exports: unknown) => { compile(input: string): string; version(): string } };
        };
        const setup = solcMod.setupMethods ?? solcMod.default?.setupMethods;
        if (typeof setup !== 'function') throw new Error('solc.setupMethods missing');

        const compiler = setup(compiled.exports);
        compiler.version();

        parentPort.postMessage({ type: 'ready' });
        parentPort.on('message', (msg: CompileRequest) => {
            try {
                const output = compiler.compile(msg.input);
                parentPort!.postMessage({ type: 'result', id: msg.id, ok: true, output });
            } catch (err) {
                parentPort!.postMessage({
                    type: 'result',
                    id: msg.id,
                    ok: false,
                    error: err instanceof Error ? err.message : String(err),
                });
            }
        });
    } catch (err) {
        parentPort.postMessage({
            type: 'error',
            error: err instanceof Error ? err.message : String(err),
        });
    }
}

main();
