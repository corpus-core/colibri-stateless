/**
 * Module-resolution hook for the WebLLM unit tests.
 *
 * Registered with `module.register` (Node 20). The hook thread only redirects
 * the specifier. The mock module evaluates on the main thread, so it can see
 * `globalThis.__colibriExplainerWebllmFactory`.
 *
 * @param specifier - Module specifier being imported
 * @param context - Resolver context from Node
 * @param nextResolve - Default resolver
 * @return Resolved module URL
 */
export async function resolve(specifier, context, nextResolve) {
    if (specifier === '@mlc-ai/web-llm') {
        return {
            shortCircuit: true,
            url: new URL('./webllm-mock.mjs', import.meta.url).href,
        };
    }
    return nextResolve(specifier, context);
}
