/**
 * Dedicated worker that owns the WebLLM engine.
 *
 * The page only talks to the proxy returned by `CreateWebWorkerMLCEngine`.
 * Model startup and token generation run here, so they do not block the UI thread.
 */
import { WebWorkerMLCEngineHandler } from '@mlc-ai/web-llm';

const handler = new WebWorkerMLCEngineHandler();
self.onmessage = (msg: MessageEvent) => {
    handler.onmessage(msg);
};
