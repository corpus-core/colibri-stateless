/**
 * Module-resolution hook for the WebLLM unit tests.
 *
 * Registered with `module.register` (Node 20). The hook thread only redirects
 * the specifier. The mock module evaluates on the main thread, so it can see
 * `globalThis.__colibriExplainerWebllmFactory`.
 *
 * A mode file next to this hook selects an alternate mock. The hook reads it
 * on every resolve (Node caches modules by URL, not by specifier), so a test
 * can hide `CreateWebWorkerMLCEngine` or fail the import without a production hook.
 * `hide` loads the mock that has no worker API. `missing` loads a module that
 * fails to evaluate. Any other content, including a missing file, loads the
 * normal mock.
 */
import fs from 'node:fs';

const modeFile = new URL('./.webllm-import-mode', import.meta.url);

/**
 * @return Trimmed mode file contents, or `''` when the file is absent
 */
function webllmImportMode() {
    try {
        return fs.readFileSync(modeFile, 'utf8').trim();
    } catch {
        return '';
    }
}

/**
 * @param specifier - Module specifier being imported
 * @param context - Resolver context from Node
 * @param nextResolve - Default resolver
 * @return Resolved module URL
 */
export async function resolve(specifier, context, nextResolve) {
    if (specifier === '@mlc-ai/web-llm') {
        const mode = webllmImportMode();
        const file = mode === 'hide' ? './webllm-mock-no-worker.mjs'
            : mode === 'missing' ? './webllm-mock-missing.mjs'
            : './webllm-mock.mjs';
        return {
            shortCircuit: true,
            url: new URL(file, import.meta.url).href,
        };
    }
    return nextResolve(specifier, context);
}
