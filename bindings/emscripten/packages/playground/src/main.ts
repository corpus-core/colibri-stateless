import C4Client from '@corpus-core/colibri-stateless';
import {
    enrichSimulation,
    createProvider,
    buildPrompt,
    toEnhancedResult,
    resolveLabels,
    parseExplanation,
    formatGas,
    shortenAddress,
    labelAddress,
    hexToBigInt,
    DEFAULT_SYSTEM_PROMPT,
    TSA_EXPLAINER_MODELS,
} from '@corpus-core/colibri-explainer';
import type {
    ExplainerConfig,
    SimulationResult,
    EnrichedContext,
    EnhancedLog,
    TxParams,
    LLMProviderType,
    ExplanationLine,
    AddressLabel,
    EthCallFn,
} from '@corpus-core/colibri-explainer';
import { Transaction } from 'ethers';

// -- Model catalogs per provider --------------------------------------------

// Prebuilt WebLLM models offered as generic alternatives to the fine-tunes.
// Approximate one-time download size (q4f16_1 weights) is shown in the
// dropdown so users can gauge the download before selecting.
const PREBUILT_WEBLLM_MODELS: { id: string; size: string }[] = [
    { id: 'Qwen2.5-Coder-7B-Instruct-q4f16_1-MLC', size: '~4.7 GB' },
    { id: 'Qwen2.5-Coder-3B-Instruct-q4f16_1-MLC', size: '~2.0 GB' },
    { id: 'Llama-3.2-3B-Instruct-q4f16_1-MLC', size: '~1.9 GB' },
    { id: 'Llama-3.2-1B-Instruct-q4f16_1-MLC', size: '~0.9 GB' },
];

// Fine-tuned explainer models first (they were trained on exactly these
// prompts), then the prebuilt fallbacks.
const MODELS: Record<LLMProviderType, string[]> = {
    webllm: [
        ...TSA_EXPLAINER_MODELS.map((r) => r.model_id),
        ...PREBUILT_WEBLLM_MODELS.map((m) => m.id),
    ],
    openai: ['gpt-4o-mini', 'gpt-4o'],
    anthropic: ['claude-sonnet-4-20250514', 'claude-3-5-haiku-latest'],
    ollama: ['qwen2.5-coder', 'llama3.1'],
};

const WEBLLM_MODEL_SIZE: Record<string, string> = Object.fromEntries([
    ...TSA_EXPLAINER_MODELS.map((r) => [r.model_id, r.download_gb ? `~${r.download_gb.toFixed(1)} GB` : '']),
    ...PREBUILT_WEBLLM_MODELS.map((m) => [m.id, m.size]),
]);

// Where the fine-tuned weights come from, in order of precedence:
//   1. `?modelUrl=http://localhost:8787/` - explicit override, e.g. a freshly
//      converted model served by `tsa_train.py serve`.
//   2. Same-origin `/models/<model_id>/<weights_version>/` - the self-hosted
//      model server behind the deployment's reverse proxy (probed once).
//   3. The record's default URL (Hugging Face).
// Model ids and WASM libraries never change; only the weight location does.
const LOCAL_MODEL_URL = new URLSearchParams(window.location.search).get('modelUrl') || '';
const SAME_ORIGIN_MODELS_PATH = '/models/';

type ModelRecord = (typeof TSA_EXPLAINER_MODELS)[number];
let resolvedModelRecords: Promise<ModelRecord[] | undefined> | undefined;

function sameOriginModelUrl(record: ModelRecord): string {
    return `${window.location.origin}${SAME_ORIGIN_MODELS_PATH}${record.model_id}/${record.weights_version ?? 'v1'}/`;
}

async function probe(url: string): Promise<boolean> {
    try {
        const res = await fetch(url, { method: 'HEAD', cache: 'no-store' });
        return res.ok;
    } catch {
        return false;
    }
}

function webllmModelRecords(): Promise<ModelRecord[] | undefined> {
    if (!resolvedModelRecords) {
        resolvedModelRecords = (async () => {
            if (LOCAL_MODEL_URL) return TSA_EXPLAINER_MODELS.map((r) => ({ ...r, model: LOCAL_MODEL_URL }));
            const records = await Promise.all(TSA_EXPLAINER_MODELS.map(async (r) => {
                const base = sameOriginModelUrl(r);
                return (await probe(`${base}mlc-chat-config.json`)) ? { ...r, model: base } : r;
            }));
            return records.some((r, i) => r !== TSA_EXPLAINER_MODELS[i]) ? records : undefined;
        })();
    }
    return resolvedModelRecords;
}

// Context window the fine-tuned records ship with; prebuilt models fall back
// to WebLLM's default (usually 4096) unless the user fills the field.
const WEBLLM_MODEL_CONTEXT: Record<string, number> = Object.fromEntries(
    TSA_EXPLAINER_MODELS
        .filter((r) => r.overrides?.context_window_size)
        .map((r) => [r.model_id, r.overrides!.context_window_size!]),
);

// -- Tiny DOM helpers --------------------------------------------------------

function $<T extends HTMLElement = HTMLElement>(id: string): T {
    const el = document.getElementById(id);
    if (!el) throw new Error(`Missing element #${id}`);
    return el as T;
}

type InputMode = 'object' | 'json' | 'raw' | 'samples';
let inputMode: InputMode = 'object';

/** One entry from `/traces/latest.json`. `meta` matches the Tx Object fields. */
interface SampleTx {
    txhash: string;
    /** Selector hex today; a decoded signature once the trace index is updated. */
    function: string;
    contract: string;
    meta: { from?: string; to?: string; input?: string; value?: string };
    path: string;
}

const TRACES_INDEX = '/traces/latest.json';
const TX_HASH_RE = /^0x[0-9a-fA-F]{64}$/;

let samples: SampleTx[] = [];
let selectedSampleHash = '';
let samplesLoaded = false;
let samplesLoading = false;
let samplesRequest = 0;

function setStatus(text: string): void {
    $('status').textContent = text;
}

function show(id: string, visible: boolean): void {
    $(id).classList.toggle('hidden', !visible);
}

// Wall-clock anchor for the current model download, used to estimate the
// remaining time. Reset at the start of every run.
let modelDownloadStart = 0;

function formatDuration(seconds: number): string {
    const s = Math.max(0, Math.round(seconds));
    if (s < 60) return `${s}s`;
    const m = Math.floor(s / 60);
    const rem = s % 60;
    return rem ? `${m}m ${rem}s` : `${m}m`;
}

/**
 * Update the model download/loading progress bar. WebLLM reports `progress`
 * (fetchedBytes / totalBytes) and a human-readable `text`; the remaining time is
 * estimated from the elapsed wall-clock time and the current progress.
 */
function setProgress(progress: number, text: string): void {
    show('progress-wrap', true);
    if (modelDownloadStart === 0) modelDownloadStart = performance.now();

    const pct = Math.max(0, Math.min(100, Math.round(progress * 100)));
    ($('progress-fill') as HTMLDivElement).style.width = `${pct}%`;
    $('progress-pct').textContent = `${pct}%`;

    // Surface the model name + size so the user understands what is being
    // fetched and why a multi-GB download can take a while.
    const model = ($('model') as HTMLSelectElement).value;
    const prettyModel = model.replace(/-q4f16_1-MLC$/, '');
    const size = WEBLLM_MODEL_SIZE[model];

    // WebLLM uses "Loading model from cache" once the weights are local and only
    // the GPU upload remains - that phase is not a download, so hide the hint.
    const isLoading = /loading model/i.test(text);
    $('progress-label').textContent = isLoading
        ? `Loading ${prettyModel} into GPU…`
        : `Downloading ${prettyModel}${size ? ` (${size})` : ''}…`;
    show('progress-hint', !isLoading);

    const elapsedSec = (performance.now() - modelDownloadStart) / 1000;
    let eta = '';
    if (progress > 0.01 && progress < 0.999 && elapsedSec > 1) {
        const remaining = (elapsedSec * (1 - progress)) / progress;
        eta = `~${formatDuration(remaining)} remaining`;
    }
    $('progress-eta').textContent = eta;
    $('progress-text').textContent = text;
}

// -- Step checklist ----------------------------------------------------------

type StepState = 'pending' | 'active' | 'done' | 'error' | 'skipped';

function setStep(step: number, state: StepState): void {
    const li = document.querySelector(`#steps li[data-step="${step}"]`);
    if (li) li.className = state;
}

function resetSteps(): void {
    show('steps', true);
    for (let i = 1; i <= 4; i++) setStep(i, 'pending');
}

/** Mark every step that has not finished (or been skipped) as errored. */
function markStepsErrored(): void {
    for (let i = 1; i <= 4; i++) {
        const li = document.querySelector(`#steps li[data-step="${i}"]`);
        if (li && li.className !== 'done' && li.className !== 'skipped') li.className = 'error';
    }
}

// -- UI wiring ---------------------------------------------------------------

function populateModels(): void {
    const provider = ($('provider') as HTMLSelectElement).value as LLMProviderType;
    const select = $('model') as HTMLSelectElement;
    select.innerHTML = '';
    for (const m of MODELS[provider]) {
        const opt = document.createElement('option');
        opt.value = m;
        const size = WEBLLM_MODEL_SIZE[m];
        opt.textContent = size ? `${m} (${size})` : m;
        select.appendChild(opt);
    }

    const isLocal = provider === 'webllm';
    if (isLocal) syncContextWindow();
    const isCloudKeyed = provider === 'openai' || provider === 'anthropic';
    const needsBaseUrl = provider === 'ollama' || isCloudKeyed;
    show('apikey-wrap', isCloudKeyed);
    show('baseurl-wrap', needsBaseUrl);
    show('apikey-row', isCloudKeyed || needsBaseUrl);
    show('contextWindow-wrap', isLocal);
}

/**
 * Prefill the context-window field with the value the selected fine-tuned
 * record ships with, and clear it for prebuilt models so WebLLM's own default
 * applies. A value the user typed for the current model is left alone.
 */
let contextWindowAutoValue = '';
function syncContextWindow(): void {
    const input = $('contextWindow') as HTMLInputElement;
    if (input.value && input.value !== contextWindowAutoValue) return;
    const model = ($('model') as HTMLSelectElement).value;
    contextWindowAutoValue = WEBLLM_MODEL_CONTEXT[model] ? String(WEBLLM_MODEL_CONTEXT[model]) : '';
    input.value = contextWindowAutoValue;
}

// The oblivious-node option only applies to PAP, so it is hidden unless privacy
// mode is enabled.
function updatePrivacyVisibility(): void {
    show('oblivious-wrap', ($('privacy') as HTMLInputElement).checked);
}

function bindTabs(): void {
    for (const tab of Array.from(document.querySelectorAll('.tab'))) {
        tab.addEventListener('click', () => {
            for (const t of Array.from(document.querySelectorAll('.tab'))) t.classList.remove('active');
            tab.classList.add('active');
            inputMode = (tab as HTMLElement).dataset.mode as InputMode;
            show('mode-object', inputMode === 'object');
            show('mode-json', inputMode === 'json');
            show('mode-raw', inputMode === 'raw');
            show('mode-samples', inputMode === 'samples');
            syncInputModeChrome();
            if (inputMode === 'samples' && !samplesLoaded && !samplesLoading) void loadSamples();
        });
    }
}

function syncInputModeChrome(): void {
    const samples = inputMode === 'samples';
    $('step-sim-label').textContent = samples ? 'Recorded trace loaded' : 'Tx locally simulated';
    $('run').textContent = samples ? 'Explain' : 'Simulate & Explain';
}

// -- Transaction parsing -----------------------------------------------------

function normalizeHexValue(v: string): string {
    const t = v.trim();
    if (!t) return '0x0';
    if (t.startsWith('0x')) return t;
    // Treat plain decimal input as wei.
    return '0x' + BigInt(t).toString(16);
}

function readTxFromObject(): TxParams {
    const to = ($('to') as HTMLInputElement).value.trim();
    if (!to) throw new Error('Field "To" is required in Tx Object mode.');
    const from = ($('from') as HTMLInputElement).value.trim();
    const value = ($('value') as HTMLInputElement).value.trim();
    const gas = ($('gas') as HTMLInputElement).value.trim();
    const data = ($('data') as HTMLTextAreaElement).value.trim();

    const tx: TxParams = { to };
    if (from) tx.from = from;
    if (value) tx.value = normalizeHexValue(value);
    if (gas) tx.gas = gas.startsWith('0x') ? gas : '0x' + BigInt(gas).toString(16);
    if (data) tx.data = data;
    return tx;
}

function readTxFromRaw(): TxParams {
    const raw = ($('rawtx') as HTMLTextAreaElement).value.trim();
    if (!raw) throw new Error('Raw transaction is required in Raw mode.');
    const parsed = Transaction.from(raw);
    if (!parsed.to) throw new Error('Raw transaction has no recipient (contract creation is not supported).');

    const tx: TxParams = { to: parsed.to };
    if (parsed.from) tx.from = parsed.from;
    if (parsed.value > 0n) tx.value = '0x' + parsed.value.toString(16);
    if (parsed.data && parsed.data !== '0x') tx.data = parsed.data;
    if (parsed.gasLimit > 0n) tx.gas = '0x' + parsed.gasLimit.toString(16);
    return tx;
}

/**
 * Parses a pasted JSON transaction object. Accepts the common wallet/explorer
 * shape (`{to, from, value, data, gas}`), the RPC alias fields (`input`,
 * `gasLimit`), and even a full JSON-RPC request (`{method, params:[tx, block]}`),
 * from which the first param object is used.
 */
function readTxFromJson(): TxParams {
    const text = ($('jsontx') as HTMLTextAreaElement).value.trim();
    if (!text) throw new Error('Transaction JSON is required in JSON mode.');

    let obj: any;
    try {
        obj = JSON.parse(text);
    } catch (e) {
        throw new Error('Invalid JSON: ' + (e as Error).message);
    }

    // Unwrap a JSON-RPC request payload, e.g. {"method":"eth_call","params":[{...}, "latest"]}.
    if (obj && Array.isArray(obj.params) && obj.params[0] && typeof obj.params[0] === 'object') obj = obj.params[0];
    if (!obj || typeof obj !== 'object' || Array.isArray(obj)) throw new Error('JSON must be a transaction object.');

    const to = typeof obj.to === 'string' ? obj.to.trim() : '';
    if (!to) throw new Error('Transaction JSON must contain a "to" address.');

    const tx: TxParams = { to };
    if (typeof obj.from === 'string' && obj.from.trim()) tx.from = obj.from.trim();

    if (obj.value !== undefined && obj.value !== null && obj.value !== '') tx.value = normalizeHexValue(String(obj.value));

    const data = obj.data ?? obj.input;
    if (typeof data === 'string' && data.trim() && data.trim() !== '0x') tx.data = data.trim();

    const gas = obj.gas ?? obj.gasLimit;
    if (gas !== undefined && gas !== null && gas !== '') tx.gas = normalizeHexValue(String(gas));

    return tx;
}

// -- Recorded sample traces --------------------------------------------------

/** `0x` + 6 hex chars, four dots, then the last 6 hex chars. */
function shortenTxHash(hash: string): string {
    if (!TX_HASH_RE.test(hash)) return hash;
    return `${hash.slice(0, 8)}....${hash.slice(-6)}`;
}

/**
 * Relative path under `/traces`. Rejects absolute URLs and `..` so a poisoned
 * index cannot make the page fetch an arbitrary origin.
 */
function isSafeTracePath(path: string): boolean {
    if (!path || path.length > 512) return false;
    if (path.startsWith('/') || path.includes('\\') || path.includes('..') || path.includes('//')) return false;
    // Function names may land in a path segment (`transfer(address,uint256)`).
    return /^[A-Za-z0-9._/(),-]+$/.test(path);
}

function stringField(value: unknown): string | undefined {
    return typeof value === 'string' && value.trim() ? value : undefined;
}

function parseSamples(value: unknown): SampleTx[] {
    if (!Array.isArray(value)) throw new Error('Sample index is not a list of transactions.');
    const out: SampleTx[] = [];
    for (const item of value) {
        if (!item || typeof item !== 'object') continue;
        const rec = item as Record<string, unknown>;
        const txhash = typeof rec.txhash === 'string' ? rec.txhash : '';
        const path = typeof rec.path === 'string' ? rec.path : '';
        if (!TX_HASH_RE.test(txhash) || !isSafeTracePath(path)) continue;
        const metaRaw = rec.meta && typeof rec.meta === 'object' ? rec.meta as Record<string, unknown> : {};
        out.push({
            txhash,
            function: typeof rec.function === 'string' ? rec.function : '',
            contract: typeof rec.contract === 'string' ? rec.contract.trim() : '',
            meta: {
                from: stringField(metaRaw.from),
                to: stringField(metaRaw.to),
                input: stringField(metaRaw.input),
                value: stringField(metaRaw.value),
            },
            path,
        });
    }
    if (value.length > 0 && out.length === 0) {
        throw new Error('Sample index did not contain any usable transactions.');
    }
    return out;
}

function selectedSample(): SampleTx | undefined {
    return samples.find((sample) => sample.txhash === selectedSampleHash);
}

/** Map a sample's `meta` onto the same fields the Tx Object tab edits. */
function txFromSample(sample: SampleTx): TxParams {
    const to = sample.meta.to?.trim() ?? '';
    if (!to) throw new Error('Selected sample has no "to" address.');
    const tx: TxParams = { to };
    const from = sample.meta.from?.trim();
    const value = sample.meta.value?.trim();
    const data = sample.meta.input?.trim();
    if (from) tx.from = from;
    if (value) tx.value = normalizeHexValue(value);
    if (data) tx.data = data;
    return tx;
}

function isSimulationResult(value: unknown): value is SimulationResult {
    if (!value || typeof value !== 'object') return false;
    const rec = value as Record<string, unknown>;
    return typeof rec.gasUsed === 'string'
        && typeof rec.status === 'string'
        && typeof rec.returnValue === 'string'
        && Array.isArray(rec.logs);
}

async function fetchSampleSimulation(path: string): Promise<SimulationResult> {
    if (!isSafeTracePath(path)) throw new Error('Sample trace path is not a relative path under /traces.');
    const url = '/traces/' + path.split('/').map((part) => encodeURIComponent(part)).join('/');
    const res = await fetch(url, { cache: 'no-store' });
    if (!res.ok) throw new Error(`Failed to load recorded trace (${res.status}).`);
    const body: unknown = await res.json();
    if (!isSimulationResult(body)) throw new Error('Recorded trace is not a colibri_simulateTransaction result.');
    return body;
}

function renderSampleFields(): void {
    const sample = selectedSample();
    show('sample-fields', !!sample);
    if (!sample) return;
    const link = $('sample-txhash') as HTMLAnchorElement;
    link.textContent = sample.txhash;
    link.href = TX_HASH_RE.test(sample.txhash) ? `https://etherscan.io/tx/${sample.txhash}` : '#';
    $('sample-txhash-copy').textContent = 'Copy';
    ($('sample-to') as HTMLInputElement).value = sample.meta.to ?? '';
    ($('sample-from') as HTMLInputElement).value = sample.meta.from ?? '';
    ($('sample-value') as HTMLInputElement).value = sample.meta.value ?? '';
    ($('sample-data') as HTMLTextAreaElement).value = sample.meta.input ?? '';
}

let copyResetTimer = 0;

function flashCopyButton(label: string): void {
    const button = $('sample-txhash-copy');
    button.textContent = label;
    window.clearTimeout(copyResetTimer);
    copyResetTimer = window.setTimeout(() => {
        if (button.textContent === label) button.textContent = 'Copy';
    }, 1500);
}

/**
 * Copy text from a click handler. The async Clipboard API is tried first; some
 * browsers reject it, so a selected textarea plus `execCommand` is the fallback.
 */
function copyText(text: string): Promise<void> {
    const viaClipboard = navigator.clipboard?.writeText(text);
    if (viaClipboard) {
        return viaClipboard.catch(() => copyTextFallback(text));
    }
    return copyTextFallback(text);
}

function copyTextFallback(text: string): Promise<void> {
    const area = document.createElement('textarea');
    area.value = text;
    area.setAttribute('readonly', '');
    area.style.position = 'fixed';
    area.style.top = '0';
    area.style.left = '0';
    area.style.opacity = '0';
    document.body.appendChild(area);
    area.focus();
    area.select();
    const ok = document.execCommand('copy');
    area.remove();
    return ok ? Promise.resolve() : Promise.reject(new Error('copy failed'));
}

function bindSampleHashCopy(): void {
    $('sample-txhash-copy').addEventListener('click', () => {
        const hash = $('sample-txhash').textContent?.trim() ?? '';
        if (!TX_HASH_RE.test(hash)) return;
        void copyText(hash).then(
            () => flashCopyButton('Copied'),
            () => flashCopyButton('Copy failed'),
        );
    });
}

function renderSamples(): void {
    const list = $('samples-list');
    const scroll = list.scrollTop;
    list.replaceChildren();
    show('samples-list', samples.length > 0);
    if (samples.length === 0) return;

    const head = document.createElement('div');
    head.className = 'sample-head';
    head.setAttribute('aria-hidden', 'true');
    for (const label of ['Tx hash', 'Contract', 'Function']) {
        const span = document.createElement('span');
        span.textContent = label;
        head.appendChild(span);
    }
    list.appendChild(head);

    for (const sample of samples) {
        const row = document.createElement('button');
        row.type = 'button';
        row.className = 'sample-row' + (sample.txhash === selectedSampleHash ? ' selected' : '');
        row.setAttribute('role', 'option');
        row.setAttribute('aria-selected', sample.txhash === selectedSampleHash ? 'true' : 'false');
        row.title = sample.txhash;

        const hash = document.createElement('span');
        hash.className = 'sample-hash';
        hash.textContent = shortenTxHash(sample.txhash);
        row.appendChild(hash);

        const contract = document.createElement('span');
        contract.className = 'sample-contract';
        contract.textContent = sample.contract;
        if (sample.contract) contract.title = sample.contract;
        row.appendChild(contract);

        const fn = document.createElement('span');
        fn.className = 'sample-fn';
        fn.textContent = sample.function;
        if (sample.function) fn.title = sample.function;
        row.appendChild(fn);

        row.addEventListener('click', () => {
            selectedSampleHash = sample.txhash;
            renderSamples();
            renderSampleFields();
        });
        list.appendChild(row);
    }
    list.scrollTop = scroll;
}

async function loadSamples(): Promise<void> {
    const request = ++samplesRequest;
    samplesLoading = true;
    const refresh = $('samples-refresh') as HTMLButtonElement;
    refresh.disabled = true;
    $('samples-status').textContent = 'Loading samples…';
    try {
        const res = await fetch(TRACES_INDEX, { cache: 'no-store' });
        if (!res.ok) throw new Error(`Failed to load samples (${res.status}).`);
        const body: unknown = await res.json();
        if (request !== samplesRequest) return;
        samples = parseSamples(body);
        samplesLoaded = true;
        if (selectedSampleHash && !samples.some((sample) => sample.txhash === selectedSampleHash)) {
            selectedSampleHash = '';
        }
        renderSamples();
        renderSampleFields();
        $('samples-status').textContent = samples.length
            ? `${samples.length} recorded transactions.`
            : 'No recorded transactions.';
    } catch (err) {
        if (request !== samplesRequest) return;
        samplesLoaded = false;
        $('samples-status').textContent = err instanceof Error ? err.message : String(err);
    } finally {
        if (request === samplesRequest) {
            samplesLoading = false;
            refresh.disabled = false;
        }
    }
}

function readTransaction(): TxParams {
    if (inputMode === 'samples') {
        const sample = selectedSample();
        if (!sample) throw new Error('Select a sample transaction first.');
        return txFromSample(sample);
    }
    if (inputMode === 'object') return readTxFromObject();
    if (inputMode === 'json') return readTxFromJson();
    return readTxFromRaw();
}

function makeClient(chainId: number, rpc: string, prover: string, usePrivacy: boolean): C4Client {
    const clientConfig: Record<string, unknown> = { chainId };
    clientConfig.zk_proof = true;
    clientConfig.debug = true;
    clientConfig.skip_wsp_check = true;
    if (rpc) clientConfig.rpcs = [rpc];
    if (prover) clientConfig.prover = [prover];
    if (usePrivacy) {
        // Pragmatic Adaptive Privacy: hides which account/storage is requested
        // via a TEE-backed hybrid prover and ZK-verified state proofs.
        clientConfig.privacy_mode = 'basic';
        clientConfig.prover_mode = 'hybrid';
        // Optional: route the privacy-critical eth_getProof requests to a TEE
        // (ORAM) node so even the requested storage keys are not leaked.
        const oblivious = ($('oblivious') as HTMLInputElement).value.trim();
        if (oblivious) clientConfig.oblivious_nodes = [oblivious];
    }
    return new C4Client(clientConfig);
}

function ethCallFor(client: C4Client): EthCallFn {
    return async (to, data) => {
        try {
            const out = await client.rpc('eth_call', [{ to, data }, 'latest']);
            return typeof out === 'string' ? out : null;
        } catch {
            return null;
        }
    };
}

async function buildExplainerConfig(useEnrichment: boolean, chainId: number): Promise<ExplainerConfig> {
    const provider = ($('provider') as HTMLSelectElement).value as LLMProviderType;
    const model = ($('model') as HTMLSelectElement).value;
    const apiKey = ($('apiKey') as HTMLInputElement).value.trim();
    const baseUrl = ($('baseUrl') as HTMLInputElement).value.trim();
    const language = ($('language') as HTMLSelectElement).value.trim() || 'en';
    const maxSourceChars = Number(($('maxSourceChars') as HTMLInputElement).value) || undefined;
    const ctx = Number(($('contextWindow') as HTMLInputElement).value) || undefined;
    const systemPrompt = ($('systemPrompt') as HTMLTextAreaElement).value.trim();

    const config: ExplainerConfig = {
        provider,
        model,
        language,
        maxSourceChars,
        chainId: useEnrichment ? chainId : undefined,
        explainMode: 'developer',
    };
    if (apiKey) config.apiKey = apiKey;
    if (baseUrl) config.baseUrl = baseUrl;
    if (systemPrompt) config.systemPrompt = systemPrompt;
    if (provider === 'webllm') {
        if (ctx) config.contextWindowSize = ctx;
        const records = await webllmModelRecords();
        if (records) config.webllmModelRecords = [...records];
        config.onModelProgress = ({ progress, text }) => setProgress(progress, text);
        // Render the answer live as the local model streams it. Once tokens
        // arrive the download/load is finished, so the progress bar can go away.
        config.onToken = () => {
            show('progress-wrap', false);
        };
        config.onLine = (line) => {
            show('progress-wrap', false);
            appendLine(line);
        };
    }
    return config;
}

// -- Run ---------------------------------------------------------------------

let lineLabels: AddressLabel[] = [];
let lineChainId = 1;

const EMPTY_CONTEXT: EnrichedContext = {
    contracts: new Map(),
    resolvedStorage: new Map(),
    decodedTrace: [],
    decodedEvents: [],
};

async function run(): Promise<void> {
    show('result', false);
    show('error', false);
    show('progress-wrap', false);
    modelDownloadStart = 0;
    clearDebug();
    resetSteps();
    const runBtn = $('run') as HTMLButtonElement;
    runBtn.disabled = true;

    // Keep partial results so the debug view stays useful even when a later
    // step fails (e.g. the LLM exceeds its context window).
    let sim: SimulationResult | undefined;
    let context: EnrichedContext = EMPTY_CONTEXT;
    let ethCall: EthCallFn | undefined;

    try {
        const chainId = Number(($('chainId') as HTMLSelectElement).value);
        if (!Number.isFinite(chainId) || chainId <= 0) throw new Error('Invalid chain ID.');
        // The RPC node serves JSON eth_* responses; the prover returns SSZ-encoded
        // proofs. They are distinct roles, so they are configured independently.
        const rpc = ($('rpc') as HTMLInputElement).value.trim();
        const prover = ($('prover') as HTMLInputElement).value.trim();
        const useEnrichment = ($('enrich') as HTMLInputElement).checked;
        const usePrivacy = ($('privacy') as HTMLInputElement).checked;

        const tx = readTransaction();
        // Capture the path before any await. A refresh while the explainer
        // config is built must not swap the trace out from under this run.
        const tracePath = inputMode === 'samples' ? selectedSample()?.path : undefined;
        const config = await buildExplainerConfig(useEnrichment, chainId);

        // Step 1 + 2: verified local simulation, or a trace that was already
        // produced by colibri_simulateTransaction and stored under /traces.
        setStep(1, 'active');
        if (tracePath) {
            setStatus('Loading recorded simulation...');
            sim = await fetchSampleSimulation(tracePath);
        } else if (inputMode === 'samples') {
            throw new Error('Select a sample transaction first.');
        } else {
            setStatus('Running colibri_simulateTransaction (verified)...');
            const client = makeClient(chainId, rpc, prover, usePrivacy);
            sim = (await client.rpc('colibri_simulateTransaction', [tx, 'latest'])) as SimulationResult;
            ethCall = ethCallFor(client);
        }
        $('sim-json').textContent = JSON.stringify(sim, null, 2);
        setStep(1, 'done');
        // The recorded trace is already the simulateTransaction result, so this
        // browser does not verify consensus or state again.
        setStep(2, tracePath ? 'skipped' : 'done');

        // Step 3: fetch + compile + verify contract sources (Sourcify enrichment).
        if (useEnrichment) {
            setStatus('Fetching, compiling and verifying contract sources...');
            setStep(3, 'active');
            context = await enrichSimulation(sim, tx, chainId, { sourcifyBaseUrl: config.sourcifyBaseUrl });
            setStep(3, 'done');
        } else {
            setStep(3, 'skipped');
        }

        // The decoded result and the prompt are fully known before the LLM runs,
        // so render them now -- they remain visible even if the LLM step fails.
        context.labels = await resolveLabels(sim, tx, {
            contracts: context.contracts,
            resolvedStorage: context.resolvedStorage,
            decodedEvents: context.decodedEvents,
            ethCall,
        });
        lineLabels = Object.values(context.labels.byAddress);
        lineChainId = chainId;
        resetLines();

        renderDecoded(sim, context);
        const prompt = buildPrompt(sim, tx, config, context);
        showPrompt(prompt.systemPrompt, prompt.userPrompt);
        show('result', true);

        // Step 4: LLM explanation (downloads the local model on first use).
        setStatus('Generating explanation...');
        setStep(4, 'active');
        const provider = createProvider(config);
        const explanation = await provider.complete(prompt.systemPrompt, prompt.userPrompt);
        setStep(4, 'done');

        renderLines(parseExplanation(explanation, prompt.refs));
        $('enhanced-json').textContent = JSON.stringify(toEnhancedResult(sim, context, explanation, prompt.refs), null, 2);
        show('progress-wrap', false);
        setStatus('Done.');
    } catch (err) {
        const msg = err instanceof Error ? (err.stack || err.message) : String(err);
        $('error-text').textContent = msg;
        show('error', true);
        markStepsErrored();
        setStatus('Failed.');
        // Surface whatever we already computed so the failure can be inspected.
        if (sim) {
            show('result', true);
            ($('debug') as HTMLDetailsElement).open = true;
            if (!$('explanation').textContent) {
                $('explanation').textContent = '(no explanation - the step above failed; see Debug data below)';
            }
        }
    } finally {
        runBtn.disabled = false;
    }
}

// -- Result rendering --------------------------------------------------------

function clearDebug(): void {
    for (const id of ['explanation', 'explanation-more', 'prompt-text', 'prompt-size', 'enhanced-json', 'sim-json']) {
        $(id).textContent = '';
    }
    show('explanation-details', false);
    $('summary').innerHTML = '';
    $('events').innerHTML = '';
}

/** Approximate token count (~4 chars/token) -- good enough for budgeting. */
function approxTokens(chars: number): number {
    return Math.ceil(chars / 4);
}

function showPrompt(systemPrompt: string, userPrompt: string): void {
    const sys = systemPrompt.length;
    const usr = userPrompt.length;
    const total = sys + usr;
    $('prompt-size').textContent =
        `${total.toLocaleString()} chars · ~${approxTokens(total).toLocaleString()} tokens` +
        ` (system ${sys.toLocaleString()} · user ${usr.toLocaleString()})`;
    $('prompt-text').textContent = `=== SYSTEM ===\n${systemPrompt}\n\n=== USER ===\n${userPrompt}`;
}

function renderDecoded(sim: SimulationResult, context: EnrichedContext): void {
    const enhanced = toEnhancedResult(sim, context, '');
    $('enhanced-json').textContent = JSON.stringify(enhanced, null, 2);

    const succeeded = enhanced.status === '0x1';
    const summary = $('summary');
    summary.innerHTML = '';
    summary.appendChild(badge(succeeded ? 'SUCCESS' : 'REVERTED', succeeded ? 'ok' : 'bad'));
    summary.appendChild(meta('Gas used', formatGas(enhanced.gasUsed)));
    if (enhanced.decodedCall) summary.appendChild(meta('Function', enhanced.decodedCall.name));
    if (enhanced.error?.reason) summary.appendChild(meta('Revert', enhanced.error.reason));

    renderEvents(enhanced.logs || []);
}

function renderEvents(logs: EnhancedLog[]): void {
    const list = $('events');
    list.innerHTML = '';
    show('events-heading', logs.length > 0);
    show('events', logs.length > 0);

    for (const log of logs) {
        const li = document.createElement('li');
        const addr = log.raw?.address ? labelAddress(log.raw.address, shortenAddress) : 'unknown';
        const decoded = log.decoded;
        const params = decoded?.params ?? log.inputs ?? [];

        const name = decoded?.name ?? log.name;
        const head = document.createElement('div');
        head.className = 'event-head';
        if (name) {
            const strong = document.createElement('strong');
            strong.textContent = name;
            head.appendChild(strong);
        } else {
            const topic0 = log.raw?.topics?.[0];
            head.textContent = topic0 ? `Unknown (${shortenAddress(topic0)})` : 'Unknown event';
        }
        const on = document.createElement('span');
        on.className = 'event-addr';
        on.textContent = ` on ${addr}`;
        head.appendChild(on);
        li.appendChild(head);

        if (params.length > 0) {
            const p = document.createElement('div');
            p.className = 'event-params';
            p.textContent = params.map(formatEventParam).join(', ');
            li.appendChild(p);
        }
        list.appendChild(li);
    }
}

function formatEventParam(param: { name: string; type: string; value: string }): string {
    let value = param.value;
    if (param.type === 'address') value = labelAddress(param.value, shortenAddress);
    else if ((param.type.startsWith('uint') || param.type.startsWith('int')) && param.value.startsWith('0x')) {
        value = hexToBigInt(param.value).toString();
    }
    return `${param.name}=${value}`;
}

// Render LLM output (which is typically Markdown) into the target element.
// The text is untrusted, so the parsed HTML is sanitized before insertion.
function resetLines(): void {
    $('explanation').replaceChildren();
    $('explanation-more').replaceChildren();
    show('explanation-details', false);
}

function renderLines(lines: ExplanationLine[]): void {
    resetLines();
    for (const line of lines) appendLine(line);
}

function appendLine(line: ExplanationLine): void {
    if (line.malformed) return;
    const primary = line.type === 'summary' || line.type === 'risk';
    const parent = $(primary ? 'explanation' : 'explanation-more');
    if (!primary) show('explanation-details', true);
    const row = document.createElement('p');
    row.className = `line line-${line.type}`;
    const kind = document.createElement('span');
    kind.className = 'line-kind';
    kind.textContent = line.type.toUpperCase();
    row.appendChild(kind);
    appendLinked(row, line.text);
    if (line.refs.length) {
        const refs = document.createElement('span');
        refs.className = 'line-refs';
        refs.textContent = line.refs.join(',');
        row.appendChild(refs);
    }
    parent.appendChild(row);
}

const EXPLORER: Record<number, string> = {
    1: 'https://etherscan.io/address/',
    11155111: 'https://sepolia.etherscan.io/address/',
    10: 'https://optimistic.etherscan.io/address/',
    8453: 'https://basescan.org/address/',
    42161: 'https://arbiscan.io/address/',
};

function appendLinked(parent: HTMLElement, text: string): void {
    let rest = text;
    while (rest) {
        let bestAt = -1;
        let best: AddressLabel | undefined;
        for (const label of lineLabels) {
            if (!label.text) continue;
            const at = rest.indexOf(label.text);
            if (at < 0) continue;
            if (best == null || at < bestAt || (at === bestAt && label.text.length > best.text.length)) {
                bestAt = at;
                best = label;
            }
        }
        if (!best || bestAt < 0) {
            parent.appendChild(document.createTextNode(rest));
            return;
        }
        if (bestAt > 0) parent.appendChild(document.createTextNode(rest.slice(0, bestAt)));
        const href = EXPLORER[lineChainId];
        if (href && /^0x[0-9a-f]{40}$/.test(best.address)) {
            const link = document.createElement('a');
            link.textContent = best.text;
            link.href = href + best.address;
            link.target = '_blank';
            link.rel = 'noopener noreferrer';
            parent.appendChild(link);
        } else {
            parent.appendChild(document.createTextNode(best.text));
        }
        rest = rest.slice(bestAt + best.text.length);
    }
}

function badge(text: string, kind: 'ok' | 'bad'): HTMLElement {
    const el = document.createElement('span');
    el.className = `badge ${kind}`;
    el.textContent = text;
    return el;
}

function meta(label: string, value: string): HTMLElement {
    const el = document.createElement('span');
    el.className = 'meta';
    el.innerHTML = `<span class="meta-label">${label}:</span> `;
    el.appendChild(document.createTextNode(value));
    return el;
}

// -- Bootstrap ---------------------------------------------------------------

function init(): void {
    bindTabs();
    populateModels();
    updatePrivacyVisibility();
    $('provider').addEventListener('change', populateModels);
    $('model').addEventListener('change', () => {
        if (($('provider') as HTMLSelectElement).value === 'webllm') syncContextWindow();
    });
    $('privacy').addEventListener('change', updatePrivacyVisibility);
    // Load the built-in base prompt into the textarea as an editable template.
    $('load-default-prompt').addEventListener('click', (e) => {
        e.preventDefault();
        ($('systemPrompt') as HTMLTextAreaElement).value = DEFAULT_SYSTEM_PROMPT;
    });
    $('run').addEventListener('click', () => void run());
    $('samples-refresh').addEventListener('click', () => void loadSamples());
    bindSampleHashCopy();

    if (typeof navigator !== 'undefined' && !(navigator as { gpu?: unknown }).gpu) {
        setStatus('Note: WebGPU not detected - the local provider will not work in this browser.');
    } else if (LOCAL_MODEL_URL) {
        setStatus(`Fine-tuned model weights are loaded from ${LOCAL_MODEL_URL} (modelUrl override).`);
    } else {
        // Probe the same-origin model server early so the first run does not
        // pay for it and the user sees where the weights come from.
        void webllmModelRecords().then((records) => {
            const local = records?.filter((r, i) => r !== TSA_EXPLAINER_MODELS[i]) ?? [];
            if (local.length) setStatus(`Fine-tuned model weights are served from ${window.location.origin}${SAME_ORIGIN_MODELS_PATH} (${local.map((r) => r.model_id).join(', ')}).`);
        });
    }
}

init();
