/**
 * Stand-in that fails evaluation, so the provider's missing-dependency branch
 * runs under Node. Selected when `.webllm-import-mode` contains `missing`.
 */
throw new Error('Cannot find package @mlc-ai/web-llm');
