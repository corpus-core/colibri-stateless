import C4Client from '@corpus-core/colibri-stateless';
import {
    enrichSimulation,
    createProvider,
    buildPrompt,
    toEnhancedResult,
    formatGas,
    shortenAddress,
    labelAddress,
    hexToBigInt,
    DEFAULT_SYSTEM_PROMPT,
} from '@corpus-core/colibri-explainer';
import type {
    ExplainerConfig,
    SimulationResult,
    EnrichedContext,
    EnhancedLog,
    TxParams,
    LLMProviderType,
} from '@corpus-core/colibri-explainer';
import { Transaction } from 'ethers';
import { marked } from 'marked';
import DOMPurify from 'dompurify';

// -- Model catalogs per provider --------------------------------------------

const MODELS: Record<LLMProviderType, string[]> = {
    webllm: [
        'Qwen2.5-Coder-7B-Instruct-q4f16_1-MLC',
        'Qwen2.5-Coder-3B-Instruct-q4f16_1-MLC',
        'Llama-3.2-3B-Instruct-q4f16_1-MLC',
        'Llama-3.2-1B-Instruct-q4f16_1-MLC',
    ],
    openai: ['gpt-4o-mini', 'gpt-4o'],
    anthropic: ['claude-sonnet-4-20250514', 'claude-3-5-haiku-latest'],
    ollama: ['qwen2.5-coder', 'llama3.1'],
};

// Approximate one-time download size per local WebLLM model (q4f16_1 weights),
// shown in the dropdown so users can gauge the download before selecting. These
// are rounded approximations derived from the MLC model sizes.
const WEBLLM_MODEL_SIZE: Record<string, string> = {
    'Qwen2.5-Coder-7B-Instruct-q4f16_1-MLC': '~4.7 GB',
    'Qwen2.5-Coder-3B-Instruct-q4f16_1-MLC': '~2.0 GB',
    'Llama-3.2-3B-Instruct-q4f16_1-MLC': '~1.9 GB',
    'Llama-3.2-1B-Instruct-q4f16_1-MLC': '~0.9 GB',
};

// -- Tiny DOM helpers --------------------------------------------------------

function $<T extends HTMLElement = HTMLElement>(id: string): T {
    const el = document.getElementById(id);
    if (!el) throw new Error(`Missing element #${id}`);
    return el as T;
}

type InputMode = 'object' | 'json' | 'raw';
let inputMode: InputMode = 'object';

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
    const isCloudKeyed = provider === 'openai' || provider === 'anthropic';
    const needsBaseUrl = provider === 'ollama' || isCloudKeyed;
    show('apikey-wrap', isCloudKeyed);
    show('baseurl-wrap', needsBaseUrl);
    show('apikey-row', isCloudKeyed || needsBaseUrl);
    show('contextWindow-wrap', isLocal);
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
        });
    }
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

function buildExplainerConfig(useEnrichment: boolean, chainId: number): ExplainerConfig {
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
    };
    if (apiKey) config.apiKey = apiKey;
    if (baseUrl) config.baseUrl = baseUrl;
    if (systemPrompt) config.systemPrompt = systemPrompt;
    if (provider === 'webllm') {
        if (ctx) config.contextWindowSize = ctx;
        config.onModelProgress = ({ progress, text }) => setProgress(progress, text);
        // Render the answer live as the local model streams it. Once tokens
        // arrive the download/load is finished, so the progress bar can go away.
        config.onToken = (_delta, full) => {
            show('progress-wrap', false);
            renderMarkdown('explanation', full);
        };
    }
    return config;
}

// -- Run ---------------------------------------------------------------------

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

    try {
        const chainId = Number(($('chainId') as HTMLSelectElement).value);
        if (!Number.isFinite(chainId) || chainId <= 0) throw new Error('Invalid chain ID.');
        // The RPC node serves JSON eth_* responses; the prover returns SSZ-encoded
        // proofs. They are distinct roles, so they are configured independently.
        const rpc = ($('rpc') as HTMLInputElement).value.trim();
        const prover = ($('prover') as HTMLInputElement).value.trim();
        const useEnrichment = ($('enrich') as HTMLInputElement).checked;
        const usePrivacy = ($('privacy') as HTMLInputElement).checked;

        const tx =
            inputMode === 'object' ? readTxFromObject() : inputMode === 'json' ? readTxFromJson() : readTxFromRaw();
        const config = buildExplainerConfig(useEnrichment, chainId);

        // Step 1 + 2: verified local simulation.
        setStatus('Running colibri_simulateTransaction (verified)...');
        setStep(1, 'active');
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
        const client = new C4Client(clientConfig);
        sim = (await client.rpc('colibri_simulateTransaction', [tx, 'latest'])) as SimulationResult;
        $('sim-json').textContent = JSON.stringify(sim, null, 2);
        setStep(1, 'done');
        setStep(2, 'done');

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

        renderMarkdown('explanation', explanation);
        $('enhanced-json').textContent = JSON.stringify(toEnhancedResult(sim, context, explanation), null, 2);
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
    for (const id of ['explanation', 'prompt-text', 'prompt-size', 'enhanced-json', 'sim-json']) {
        $(id).textContent = '';
    }
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
function renderMarkdown(id: string, markdown: string): void {
    const html = marked.parse(markdown, { async: false }) as string;
    $(id).innerHTML = DOMPurify.sanitize(html);
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
    $('privacy').addEventListener('change', updatePrivacyVisibility);
    // Load the built-in base prompt into the textarea as an editable template.
    $('load-default-prompt').addEventListener('click', (e) => {
        e.preventDefault();
        ($('systemPrompt') as HTMLTextAreaElement).value = DEFAULT_SYSTEM_PROMPT;
    });
    $('run').addEventListener('click', () => void run());

    if (typeof navigator !== 'undefined' && !(navigator as { gpu?: unknown }).gpu) {
        setStatus('Note: WebGPU not detected - the local provider will not work in this browser.');
    }
}

init();
