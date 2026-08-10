// Import the main module entry point using the alias configured in webpack.config.js
import Colibri from '@c4w/index.js';

let clientNet = null;
let clientNoNet = null;
let proofData = null; // Uint8Array

function getEl(id) {
    const el = document.getElementById(id);
    if (!el) throw new Error(`Missing DOM element: #${id}`);
    return el;
}

function setOutput(text) {
    getEl('output').textContent = text;
}

function parseArgsJson() {
    const raw = String(getEl('args').value || '[]');
    try {
        const v = JSON.parse(raw);
        if (!Array.isArray(v)) throw new Error('Args JSON must be an array');
        return v;
    } catch (e) {
        throw new Error(`Invalid args JSON: ${String(e)}`);
    }
}

function getMethod() {
    const m = String(getEl('method').value || '').trim();
    if (!m) throw new Error('Method is required');
    return m;
}

function getChainId() {
    const n = Number(getEl('chainId').value);
    if (!Number.isFinite(n) || n <= 0) throw new Error('Invalid chainId');
    return n;
}

async function readSelectedProofFile() {
    const input = getEl('proofFile');
    const file = input.files && input.files[0];
    if (!file) throw new Error('No proof file selected');
    const buf = await file.arrayBuffer();
    proofData = new Uint8Array(buf);
    setOutput(`Loaded proof file: ${file.name}\nSize: ${proofData.length} bytes\nReady to verify.`);
}

function createMemoryStorage() {
    // Simple in-memory storage implementation to avoid persistent caching in localStorage.
    // Keys are strings; values are Uint8Array.
    const map = new Map();
    return {
        get: (key) => {
            const v = map.get(key);
            if (!v) return null;
            // Return a copy to prevent accidental mutation by consumers.
            return new Uint8Array(v);
        },
        set: (key, value) => {
            map.set(key, new Uint8Array(value));
        },
        del: (key) => {
            map.delete(key);
        }
    };
}

async function maybeResetStorage(resetEachRun) {
    if (!resetEachRun) return;
    await Colibri.register_storage(createMemoryStorage());
}

function getClient(allowRequests) {
    if (allowRequests) return clientNet;
    return clientNoNet;
}

async function initWasm() {
    setOutput('Loading WASM and initializing Colibri...');
    const chainId = getChainId();
    // Use default chain config (rpcs/prover/beacon_apis) for the given chainId.
    // verifyProof() might request follow-up data depending on the proof contents.
    clientNet = new Colibri({ chainId });
    clientNoNet = new Colibri({ chainId, rpcs: [], prover: [], beacon_apis: [], checkpointz: [] });

    // Start with memory storage to avoid persistent caches influencing profiling runs.
    await Colibri.register_storage(createMemoryStorage());

    // If the wrapper exposes an async init, prefer awaiting it. Otherwise, first RPC call will force init.
    setOutput('WASM initialized. Select a .ssz proof file, then click Verify.');

    getEl('btnVerifyOnce').disabled = false;
    getEl('btnVerifyMany').disabled = false;
}

async function verifyOnce(allowRequests) {
    const resetStorageEachRun = !!getEl('resetStorageEachRun').checked;
    await maybeResetStorage(resetStorageEachRun);

    const client = getClient(allowRequests);
    if (!client) throw new Error('Client not initialized. Click "Init WASM" first.');
    if (!proofData) throw new Error('No proof loaded. Select a .ssz proof file first.');

    const method = getMethod();
    const args = parseArgsJson();

    const t0 = performance.now();
    const result = await client.verifyProof(method, args, proofData);
    const t1 = performance.now();
    const ms = (t1 - t0);

    setOutput(`verifyProof completed in ${ms.toFixed(2)} ms\n\nResult:\n${JSON.stringify(result, null, 2)}`);
}

async function verifyMany(n, allowRequests) {
    const resetStorageEachRun = !!getEl('resetStorageEachRun').checked;
    const client = getClient(allowRequests);
    if (!client) throw new Error('Client not initialized. Click "Init WASM" first.');
    if (!proofData) throw new Error('No proof loaded. Select a .ssz proof file first.');
    if (!Number.isFinite(n) || n <= 0) throw new Error('Invalid run count.');

    const method = getMethod();
    const args = parseArgsJson();

    setOutput(`Running ${n} verifyProof iterations...`);

    const times = [];
    for (let i = 0; i < n; i++) {
        await maybeResetStorage(resetStorageEachRun);
        const t0 = performance.now();
        await client.verifyProof(method, args, proofData);
        const t1 = performance.now();
        times.push(t1 - t0);
    }

    const sum = times.reduce((a, b) => a + b, 0);
    const avg = sum / times.length;
    const min = Math.min(...times);
    const max = Math.max(...times);

    setOutput(
        `Done.\n` +
        `Iterations: ${n}\n` +
        `Avg: ${avg.toFixed(2)} ms\n` +
        `Min: ${min.toFixed(2)} ms\n` +
        `Max: ${max.toFixed(2)} ms\n` +
        `\nNote: verifyProof may still do follow-up requests if allowed.\n`
    );
}

document.addEventListener('DOMContentLoaded', () => {
    try {
        const btnInit = getEl('btnInit');
        const btnVerifyOnce = getEl('btnVerifyOnce');
        const btnVerifyMany = getEl('btnVerifyMany');
        const runsEl = getEl('runs');
        const allowRequestsEl = getEl('allowRequests');
        const proofFileEl = getEl('proofFile');
        const btnClearLocalStorage = getEl('btnClearLocalStorage');

        btnInit.addEventListener('click', async () => {
            try {
                btnInit.disabled = true;
                await initWasm();
            } catch (e) {
                btnInit.disabled = false;
                setOutput(`Init error: ${String(e)}`);
                console.error(e);
            }
        });

        proofFileEl.addEventListener('change', async () => {
            try {
                await readSelectedProofFile();
            } catch (e) {
                setOutput(`Proof load error: ${String(e)}`);
                console.error(e);
            }
        });

        btnClearLocalStorage.addEventListener('click', () => {
            try {
                if (typeof window !== 'undefined' && window.localStorage) {
                    window.localStorage.clear();
                }
                setOutput('localStorage cleared.');
            } catch (e) {
                setOutput(`localStorage clear error: ${String(e)}`);
                console.error(e);
            }
        });

        btnVerifyOnce.addEventListener('click', async () => {
            try {
                btnVerifyOnce.disabled = true;
                await verifyOnce(allowRequestsEl.checked);
            } catch (e) {
                setOutput(`Run error: ${String(e)}`);
                console.error(e);
            } finally {
                btnVerifyOnce.disabled = false;
            }
        });

        btnVerifyMany.addEventListener('click', async () => {
            try {
                btnVerifyMany.disabled = true;
                const n = Number(runsEl.value);
                await verifyMany(n, allowRequestsEl.checked);
            } catch (e) {
                setOutput(`Run error: ${String(e)}`);
                console.error(e);
            } finally {
                btnVerifyMany.disabled = false;
            }
        });
    } catch (e) {
        console.error(e);
    }
});