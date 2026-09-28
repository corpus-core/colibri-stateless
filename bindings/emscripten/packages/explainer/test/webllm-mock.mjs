/**
 * Stand-in for `@mlc-ai/web-llm` used by the explainer unit tests.
 * `CreateMLCEngine` delegates to the factory the current test installed.
 */

export const prebuiltAppConfig = { model_list: [] };
export const modelLibURLPrefix = 'https://libs.example/';
export const modelVersion = 'v1';

/**
 * @param model - Model id passed to `CreateMLCEngine`
 * @param config - Engine config
 * @param chatOpts - Chat options, including `context_window_size`
 * @return Whatever the installed test factory returns
 */
export function CreateMLCEngine(model, config, chatOpts) {
    const factory = globalThis.__colibriExplainerWebllmFactory;
    if (typeof factory !== 'function') {
        throw new Error('WebLLM test factory is not installed');
    }
    return factory(model, config, chatOpts);
}
