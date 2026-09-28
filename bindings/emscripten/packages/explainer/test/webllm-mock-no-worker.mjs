/**
 * WebLLM mock without `CreateWebWorkerMLCEngine`.
 * Selected by the test loader when `.webllm-import-mode` contains `hide`.
 */
export {
    prebuiltAppConfig,
    modelLibURLPrefix,
    modelVersion,
    CreateMLCEngine,
} from './webllm-mock.mjs';
