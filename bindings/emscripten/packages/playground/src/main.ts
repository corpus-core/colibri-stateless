import C4Client from '@corpus-core/colibri-stateless';
import { enhanceSimulation } from '@corpus-core/colibri-explainer';
import type {
    ExplainerConfig,
    SimulationResult,
    TxParams,
    LLMProviderType,
} from '@corpus-core/colibri-explainer';
import { Transaction } from 'ethers';

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

// -- Tiny DOM helpers --------------------------------------------------------

function $<T extends HTMLElement = HTMLElement>(id: string): T {
    const el = document.getElementById(id);
    if (!el) throw new Error(`Missing element #${id}`);
    return el as T;
}

let inputMode: 'object' | 'raw' = 'object';

function setStatus(text: string): void {
    $('status').textContent = text;
}

function show(id: string, visible: boolean): void {
    $(id).classList.toggle('hidden', !visible);
}

function setProgress(progress: number, text: string): void {
    show('progress-wrap', true);
    ($('progress-fill') as HTMLDivElement).style.width = `${Math.round(progress * 100)}%`;
    $('progress-text').textContent = text;
}

// -- UI wiring ---------------------------------------------------------------

function populateModels(): void {
    const provider = ($('provider') as HTMLSelectElement).value as LLMProviderType;
    const select = $('model') as HTMLSelectElement;
    select.innerHTML = '';
    for (const m of MODELS[provider]) {
        const opt = document.createElement('option');
        opt.value = m;
        opt.textContent = m;
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

function bindTabs(): void {
    for (const tab of Array.from(document.querySelectorAll('.tab'))) {
        tab.addEventListener('click', () => {
            for (const t of Array.from(document.querySelectorAll('.tab'))) t.classList.remove('active');
            tab.classList.add('active');
            inputMode = (tab as HTMLElement).dataset.mode as 'object' | 'raw';
            show('mode-object', inputMode === 'object');
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

function buildExplainerConfig(useEnrichment: boolean, chainId: number): ExplainerConfig {
    const provider = ($('provider') as HTMLSelectElement).value as LLMProviderType;
    const model = ($('model') as HTMLSelectElement).value;
    const apiKey = ($('apiKey') as HTMLInputElement).value.trim();
    const baseUrl = ($('baseUrl') as HTMLInputElement).value.trim();
    const language = ($('language') as HTMLInputElement).value.trim() || 'en';
    const maxSourceChars = Number(($('maxSourceChars') as HTMLInputElement).value) || undefined;
    const ctx = Number(($('contextWindow') as HTMLInputElement).value) || undefined;

    const config: ExplainerConfig = {
        provider,
        model,
        language,
        maxSourceChars,
        chainId: useEnrichment ? chainId : undefined,
    };
    if (apiKey) config.apiKey = apiKey;
    if (baseUrl) config.baseUrl = baseUrl;
    if (provider === 'webllm') {
        if (ctx) config.contextWindowSize = ctx;
        config.onModelProgress = ({ progress, text }) => setProgress(progress, text);
    }
    return config;
}

// -- Run ---------------------------------------------------------------------

async function run(): Promise<void> {
    show('result', false);
    show('error', false);
    show('progress-wrap', false);
    const runBtn = $('run') as HTMLButtonElement;
    runBtn.disabled = true;

    try {
        const chainId = Number(($('chainId') as HTMLInputElement).value);
        if (!Number.isFinite(chainId) || chainId <= 0) throw new Error('Invalid chain ID.');
        const endpoint = ($('endpoint') as HTMLInputElement).value.trim();
        const useEnrichment = ($('enrich') as HTMLInputElement).checked;

        const tx = inputMode === 'object' ? readTxFromObject() : readTxFromRaw();

        setStatus('Initializing client...');
        const clientConfig: Record<string, unknown> = { chainId };
        if (endpoint) {
            clientConfig.rpcs = [endpoint];
            clientConfig.prover = [endpoint];
        }
        const client = new C4Client(clientConfig);

        setStatus('Running colibri_simulateTransaction (verified)...');
        const sim = (await client.rpc('colibri_simulateTransaction', [tx, 'latest'])) as SimulationResult;

        $('sim-json').textContent = JSON.stringify(sim, null, 2);

        setStatus('Generating explanation...');
        const config = buildExplainerConfig(useEnrichment, chainId);
        const enhanced = await enhanceSimulation(sim, tx, config);

        $('explanation').textContent = enhanced.explanation;
        $('enhanced-json').textContent = JSON.stringify(enhanced, null, 2);
        show('result', true);
        show('progress-wrap', false);
        setStatus('Done.');
    } catch (err) {
        const msg = err instanceof Error ? (err.stack || err.message) : String(err);
        $('error-text').textContent = msg;
        show('error', true);
        setStatus('Failed.');
    } finally {
        runBtn.disabled = false;
    }
}

// -- Bootstrap ---------------------------------------------------------------

function init(): void {
    bindTabs();
    populateModels();
    $('provider').addEventListener('change', populateModels);
    $('run').addEventListener('click', () => void run());

    if (typeof navigator !== 'undefined' && !(navigator as { gpu?: unknown }).gpu) {
        setStatus('Note: WebGPU not detected - the local provider will not work in this browser.');
    }
}

init();
