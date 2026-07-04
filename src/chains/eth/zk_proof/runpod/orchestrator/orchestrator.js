// orchestrator.js - watches the Colibri period-store docker volume on the
// prover server and hands off missing v5 Groth16 proofs to ephemeral RunPod
// GPU pods. See the neighbouring README.md for the full architecture.
//
// This module is intentionally dependency-light: only `@aws-sdk/client-s3`
// plus Node's built-in `fetch` (Node >= 18). The trigger condition and the
// set of artifacts written back are the same as `scripts/build_proof`, so
// this replaces the manual laptop workflow one-for-one.

const fs = require('fs');
const fsp = require('fs/promises');
const path = require('path');
const os = require('os');
const { execFile } = require('child_process');
const { pipeline } = require('stream/promises');
const {
    S3Client,
    PutObjectCommand,
    GetObjectCommand,
    DeleteObjectCommand,
} = require('@aws-sdk/client-s3');
const { NodeHttpHandler } = require('@smithy/node-http-handler');

// --- Config -----------------------------------------------------------------

// Validation of CHAIN / RUNPOD_DATACENTER is defense-in-depth: today the
// values come from the operator's docker-compose, but we interpolate them
// into filesystem paths and S3 keys, so a stray shell-metacharacter or path
// traversal would land us in the wrong tree.
const CHAIN = validateSlug('CHAIN', requireEnv('CHAIN'));
const RUNPOD_API_KEY = requireEnv('RUNPOD_API_KEY');
const RUNPOD_S3_ACCESS_KEY = requireEnv('RUNPOD_S3_ACCESS_KEY');
const RUNPOD_S3_SECRET_KEY = requireEnv('RUNPOD_S3_SECRET_KEY');
const NETWORK_VOLUME_ID = requireEnv('NETWORK_VOLUME_ID');
const RUNPOD_DATACENTER = validateSlug('RUNPOD_DATACENTER', requireEnv('RUNPOD_DATACENTER'));
const RUNPOD_IMAGE = requireEnv('RUNPOD_IMAGE');           // docker image tag for Image 2

// GPU selection. Multiple types may be given comma-separated to give RunPod
// flexibility; the SDK will pick the first available one.
const RUNPOD_GPU_TYPES = (process.env.RUNPOD_GPU_TYPES || 'NVIDIA GeForce RTX 4090')
    .split(',')
    .map((s) => s.trim())
    .filter(Boolean);

const OUTPUT_DIR = process.env.OUTPUT_DIR || '/data';
const CHECK_INTERVAL_MS = parseInt(process.env.CHECK_INTERVAL_MS || '600000', 10); // 10 min
const JOB_TIMEOUT_MS = parseInt(process.env.JOB_TIMEOUT_MS || '3600000', 10);      // 60 min
const POLL_INTERVAL_MS = parseInt(process.env.POLL_INTERVAL_MS || '15000', 10);    // 15 s
const CONTAINER_DISK_GB = parseInt(process.env.CONTAINER_DISK_GB || '20', 10);

// Quiescence window for the input `sync.ssz`. The prover-service on the server
// writes it non-atomically (fopen("wb") + write, see
// src/chains/eth/server/period_store_zk_prover.c), so a scan that catches the
// file mid-write would upload a truncated input. We only accept a `sync.ssz`
// whose mtime is at least this old, i.e. the write has provably settled. The
// file is tens of KB and written in milliseconds, so a small window is ample.
const SYNC_STABLE_MS = parseInt(process.env.SYNC_STABLE_MS || '30000', 10);        // 30 s

// Beacon-chain timing per orchestrated chain, used to work out when a period's
// proof is actually needed. All supported chains use 8192 slots per
// sync-committee period (mainnet/sepolia = 2^(5+8), gnosis/chiado = 2^(4+9);
// see chain_spec_t in src/chains/eth/ssz/beacon_types.c); only the genesis time
// and the slot duration differ.
const SLOTS_PER_PERIOD = 8192;
const CHAIN_TIMING = {
    mainnet: { genesis: 1606824023, secondsPerSlot: 12 },
    sepolia: { genesis: 1655733600, secondsPerSlot: 12 },
    gnosis:  { genesis: 1638993340, secondsPerSlot: 5 },
    chiado:  { genesis: 1665396300, secondsPerSlot: 5 },
};

// How long before a period's start we are willing to launch the (paid) GPU pod.
// The prover-service writes the next_sync_committee into `{P+1}/sync.ssz` while
// period P runs, so a `sync.ssz` found in dir <period> is due when sync
// committee period <period> begins. We hold off launching until LEAD before
// that deadline: this leaves a large window (almost a full period) for a manual
// local `scripts/build_proof` run to produce the artifacts for free, and only
// falls back to RunPod shortly before the proof is genuinely required. Set to 0
// to disable the gate and always prove as soon as inputs are present.
const PROVE_LEAD_MS = parseInt(process.env.PROVE_LEAD_MS || '3600000', 10);        // 1 h

// Optional Prometheus textfile-collector output. When set, the orchestrator
// writes a `.prom` file (atomically, via write+rename) on every scan exposing
// the last built proof and the next deadline, so Grafana can show when the
// last proof was produced and when the next one is due. Point it at a file
// inside the node_exporter textfile_collector directory (e.g.
// `/metrics/colibri_zkproof.prom`) and mount that directory into the
// container. Empty = disabled.
const METRICS_FILE = process.env.METRICS_FILE || '';

// Verify every freshly built Groth16 proof before publishing it into the
// volume. The server assembles `zk_proof.ssz` for v5 clients from these files
// but cannot verify a v5 proof itself: its own `verify_zk_proof` is pinned to
// the v6 program hash. So we gate here against the v5 verification key baked
// into the bundled `verify_zk_proof_cli` (built from this branch). Fail-closed
// - an invalid or unverifiable proof is dropped and never installed. Set
// VERIFY_PROOF=0/false to disable (e.g. for debugging).
const VERIFY_PROOF = !/^(0|false|no|off)$/i.test((process.env.VERIFY_PROOF || '1').trim());
const VERIFIER_BIN = process.env.VERIFIER_BIN || '/app/verify_zk_proof_cli';
const VERIFY_TIMEOUT_MS = parseInt(process.env.VERIFY_TIMEOUT_MS || '120000', 10); // 2 min

// Artifacts the GPU pod produces and that we need on the server. Same set as
// scripts/build_proof (UPLOAD_FILES).
const ARTIFACTS = [
    'zk_proof_g16.bin',
    'zk_pub.bin',
    'zk_proof.bin',
    'zk_vk_raw.bin',
    'zk_groth16.bin',
    'zk_vk.bin',
];

// Recursion inputs on the previous period, required to keep the chain valid
// (see docs in scripts/build_proof).
const PREV_FILES = ['zk_proof.bin', 'zk_vk_raw.bin'];

const S3_ENDPOINT = `https://s3api-${RUNPOD_DATACENTER.toLowerCase()}.runpod.io`;
// Some S3 tooling wants the datacenter id as the region string. The RunPod S3
// docs use lower-cased datacenter ids (e.g. `us-ks-2`).
const S3_REGION = RUNPOD_DATACENTER.toLowerCase();

const RUNPOD_REST = 'https://rest.runpod.io/v1';

/**
 * Read an env var, with Docker-secrets support: if `<NAME>_FILE` is set the
 * value is read from that path. This is the same convention `scripts/
 * run_zk_proof.sh` uses (see its `_FILE` branch) and lets operators wire the
 * RunPod credentials via `docker secrets` / bind mounts instead of putting
 * them into `docker inspect`-visible environment.
 */
function requireEnv(name) {
    const filePath = process.env[`${name}_FILE`];
    if (filePath) {
        try {
            const v = fs.readFileSync(filePath, 'utf8').trim();
            if (!v) throw new Error(`file is empty`);
            return v;
        } catch (e) {
            throw new Error(`Failed to read ${name} from ${filePath}: ${e.message}`);
        }
    }
    const v = process.env[name];
    if (!v) throw new Error(`Missing required environment variable: ${name} (or ${name}_FILE)`);
    return v;
}

/**
 * Validate a user-supplied string that will be interpolated into filesystem
 * paths and S3 keys. Throws on anything outside `[a-z0-9_-]`.
 */
function validateSlug(name, value) {
    if (!/^[a-zA-Z0-9_-]+$/.test(value)) {
        throw new Error(`${name} must match [a-zA-Z0-9_-]+, got '${value}'`);
    }
    return value;
}

function log(...args) {
    console.log(`[${new Date().toISOString()}]`, ...args);
}

function warn(...args) {
    console.warn(`[${new Date().toISOString()}] WARN`, ...args);
}

function err(...args) {
    console.error(`[${new Date().toISOString()}] ERROR`, ...args);
}

// --- S3 client (RunPod S3-compatible API) -----------------------------------

/**
 * RunPod's S3 gateway emits HTTP date headers (`Date`, `Last-Modified`) with a
 * `UTC` suffix, e.g. `Fri, 03 Jul 2026 21:51:16 UTC`. RFC 7231 (and the AWS SDK
 * v3 timestamp parser) mandate the `GMT` suffix, so the SDK throws
 * `Invalid RFC7231 date-time value ...` whenever it deserializes a 200 response
 * that carries `Last-Modified` (HeadObject / GetObject on an existing object).
 * We normalize the offending headers at the transport layer, before the SDK's
 * deserializer parses them. `UTC` and `GMT` both denote UTC, so the rewrite is
 * value-preserving.
 */
class RunpodDateFixHandler extends NodeHttpHandler {
    async handle(request, options) {
        const result = await super.handle(request, options);
        const headers = result.response && result.response.headers;
        if (headers) {
            for (const key of ['date', 'last-modified']) {
                const v = headers[key];
                if (typeof v === 'string' && v.endsWith(' UTC')) {
                    headers[key] = v.slice(0, -4) + ' GMT';
                }
            }
        }
        return result;
    }
}

const s3 = new S3Client({
    region: S3_REGION,
    endpoint: S3_ENDPOINT,
    credentials: {
        accessKeyId: RUNPOD_S3_ACCESS_KEY,
        secretAccessKey: RUNPOD_S3_SECRET_KEY,
    },
    // The RunPod S3 API expects path-style addressing (bucket = volume id).
    forcePathStyle: true,
    // Work around RunPod's non-RFC7231 date headers (see handler docstring).
    requestHandler: new RunpodDateFixHandler(),
});

async function s3ObjectExists(key) {
    try {
        // RunPod's S3 gateway returns spurious HTTP 403 for HeadObject on some
        // existing keys (empirically: GetObject succeeds for the very same key
        // that HeadObject 403s). GetObject is reliable, so we probe with it and
        // immediately discard the body. Markers (DONE/FAILED) are 0 bytes and
        // the other callers only check tiny objects, so this is cheap.
        const res = await s3.send(new GetObjectCommand({ Bucket: NETWORK_VOLUME_ID, Key: key }));
        if (res.Body && typeof res.Body.destroy === 'function') res.Body.destroy();
        return true;
    } catch (e) {
        const status = e && e.$metadata?.httpStatusCode;
        if (status === 404 || e?.name === 'NoSuchKey' || e?.name === 'NotFound' || e?.Code === 'NoSuchKey' || e?.Code === 'NotFound') {
            return false;
        }
        if (status === 401 || status === 403) {
            // Surface auth failures once, clearly, instead of quietly failing
            // every 15 s until JOB_TIMEOUT_MS.
            throw new Error(`RunPod S3 auth failed while checking ${key} (HTTP ${status}). Check RUNPOD_S3_* credentials.`);
        }
        throw e;
    }
}

async function s3UploadFile(localPath, key) {
    // Uploads today are tiny (sync.ssz ~50 KB, compressed proofs a few MB).
    // The RunPod S3 API caps single PutObject at 500 MB, so if any single
    // proof artifact ever grows past that we will need `@aws-sdk/lib-storage`
    // multipart uploads here. Log a warning if we approach the ceiling.
    const stat = await fsp.stat(localPath);
    if (stat.size > 400 * 1024 * 1024) {
        warn(`s3UploadFile: ${localPath} is ${stat.size} bytes - approaching the 500 MB RunPod S3 PutObject cap; consider switching to multipart`);
    }
    await s3.send(new PutObjectCommand({
        Bucket: NETWORK_VOLUME_ID,
        Key: key,
        Body: fs.createReadStream(localPath),
        ContentLength: stat.size,
    }));
}

async function s3DownloadFile(key, localPath) {
    const res = await s3.send(new GetObjectCommand({
        Bucket: NETWORK_VOLUME_ID,
        Key: key,
    }));
    // Stream directly to disk. Proof artifacts can be several hundred MB and
    // buffering them via `Buffer.concat` on chunks would double the peak
    // memory footprint. `pipeline` also handles error propagation cleanly.
    await pipeline(res.Body, fs.createWriteStream(localPath));
}

async function s3DeleteKey(key) {
    // DeleteObject is idempotent on RunPod's S3 (deleting a missing key
    // succeeds). We still tolerate the not-found variants a strict gateway
    // might return so a partially-populated job dir cleans up cleanly.
    try {
        await s3.send(new DeleteObjectCommand({ Bucket: NETWORK_VOLUME_ID, Key: key }));
    } catch (e) {
        const status = e && e.$metadata && e.$metadata.httpStatusCode;
        const label = `${e && e.name} ${e && e.message}`;
        if (status !== 404 && !/NoSuchKey|NotFound|Invalid object path/i.test(label)) {
            warn(`failed to delete ${key}: ${e.message}`);
        }
    }
}

// --- Local volume scanning --------------------------------------------------

/**
 * Scan the mounted period-store for the lowest numeric period that has
 * `sync.ssz` but no `zk_proof_g16.bin` yet, and whose previous period contains
 * the recursion inputs. Returns null when nothing needs proving.
 */
async function findMissingPeriod() {
    let entries;
    try {
        entries = await fsp.readdir(OUTPUT_DIR, { withFileTypes: true });
    } catch (e) {
        throw new Error(`cannot list ${OUTPUT_DIR}: ${e.message}`);
    }

    const periods = entries
        .filter((d) => d.isDirectory() && /^[0-9]+$/.test(d.name))
        .map((d) => parseInt(d.name, 10))
        .sort((a, b) => a - b);

    for (const period of periods) {
        const dir = path.join(OUTPUT_DIR, String(period));
        const syncStat = await statFile(path.join(dir, 'sync.ssz'));
        if (!syncStat || syncStat.size === 0) continue;
        const g16Ok = await isNonEmpty(path.join(dir, 'zk_proof_g16.bin'));
        if (g16Ok) continue;

        // Hold off until the proof is close to being needed. Launching a paid
        // GPU pod as soon as inputs appear would waste money and pre-empt a
        // manual local build; instead we defer until PROVE_LEAD_MS before the
        // period's start (its proof deadline). Deadlines grow with the period
        // number, so deferring the lowest not-yet-due period does not starve
        // any higher one. Past-due periods (e.g. backfill) launch immediately.
        const deadlineMs = periodStartMs(period);
        if (deadlineMs !== null && PROVE_LEAD_MS > 0) {
            const waitMs = deadlineMs - PROVE_LEAD_MS - Date.now();
            if (waitMs > 0) {
                log(`deferring period ${period}: proof due ${new Date(deadlineMs).toISOString()} (period start); launching in ~${Math.round(waitMs / 60000)} min (lead ${Math.round(PROVE_LEAD_MS / 60000)} min)`);
                continue;
            }
        }

        // Verify recursion inputs on the previous period. If missing, skip
        // (the chain must not be broken; the prev period has to be proved
        // first). We deliberately do NOT try to auto-fill it here - if the
        // orchestrator processes periods bottom-up it will produce the inputs
        // naturally on an earlier iteration.
        const prev = period - 1;
        const prevDir = path.join(OUTPUT_DIR, String(prev));
        const missingPrev = [];
        for (const f of PREV_FILES) {
            if (!(await isNonEmpty(path.join(prevDir, f)))) missingPrev.push(f);
        }
        if (missingPrev.length > 0) {
            const dueStr = deadlineMs !== null ? `, proof due ${new Date(deadlineMs).toISOString()}` : '';
            warn(`skipping period ${period}: previous period ${prev} is missing ${missingPrev.join(', ')} - build ${prev} first${dueStr}`);
            continue;
        }

        // Guard against reading a `sync.ssz` that the prover-service is still
        // writing. If it was modified within the last SYNC_STABLE_MS, defer it
        // to a later tick so the write can settle. Higher periods depend on
        // this one's proof, so deferring here does not starve them.
        const age = Date.now() - syncStat.mtimeMs;
        if (age < SYNC_STABLE_MS) {
            log(`deferring period ${period}: sync.ssz was modified ${Math.round(age)}ms ago (< ${SYNC_STABLE_MS}ms), waiting for the write to settle`);
            continue;
        }
        return { period, prev, deadlineMs };
    }
    return null;
}

/**
 * Epoch-ms at which sync-committee `period` begins on the configured chain -
 * i.e. the moment its proof must be available. Returns null for chains we have
 * no timing table for, in which case deadline gating is skipped.
 */
function periodStartMs(period) {
    const t = CHAIN_TIMING[CHAIN.toLowerCase()];
    if (!t) return null;
    return (t.genesis + period * SLOTS_PER_PERIOD * t.secondsPerSlot) * 1000;
}

async function statFile(p) {
    try {
        const st = await fsp.stat(p);
        if (!st.isFile()) return null;
        return { size: st.size, mtimeMs: st.mtimeMs };
    } catch {
        return null;
    }
}

async function isNonEmpty(p) {
    const st = await statFile(p);
    return st !== null && st.size > 0;
}

/**
 * Stat-only scan of the volume for monitoring: the most recent client-facing
 * proof (`zk_proof.ssz`, written by the prover-service once the artifacts are
 * present) and the next period that still needs a proof, with its deadline.
 * Returns null fields when nothing matches / the dir is unreadable.
 */
async function collectMetrics() {
    const empty = { lastProofPeriod: null, lastProofMtimeMs: null, nextPeriod: null, nextDeadlineMs: null };
    let entries;
    try {
        entries = await fsp.readdir(OUTPUT_DIR, { withFileTypes: true });
    } catch {
        return empty;
    }
    const periods = entries
        .filter((d) => d.isDirectory() && /^[0-9]+$/.test(d.name))
        .map((d) => parseInt(d.name, 10))
        .sort((a, b) => a - b);

    const m = { ...empty };
    for (const period of periods) {
        const dir = path.join(OUTPUT_DIR, String(period));
        const sszStat = await statFile(path.join(dir, 'zk_proof.ssz'));
        if (sszStat && sszStat.size > 0) {
            // Ascending order, so the last match is the highest period.
            m.lastProofPeriod = period;
            m.lastProofMtimeMs = sszStat.mtimeMs;
        }
        if (m.nextPeriod === null) {
            const syncStat = await statFile(path.join(dir, 'sync.ssz'));
            const hasG16 = await isNonEmpty(path.join(dir, 'zk_proof_g16.bin'));
            if (syncStat && syncStat.size > 0 && !hasG16) {
                m.nextPeriod = period;
                m.nextDeadlineMs = periodStartMs(period);
            }
        }
    }
    return m;
}

/**
 * Write the Prometheus textfile if METRICS_FILE is configured. The file is
 * written atomically (write to `.tmp`, then rename) as required by the
 * node_exporter textfile collector, which must never observe a partial file.
 */
async function writeMetricsFile() {
    if (!METRICS_FILE) return;
    let m;
    try {
        m = await collectMetrics();
    } catch (e) {
        warn(`metrics scan failed: ${e.message}`);
        return;
    }
    const lbl = `{chain="${CHAIN.toLowerCase()}"}`;
    const lines = [];
    const gauge = (name, help, value) => {
        lines.push(`# HELP ${name} ${help}`);
        lines.push(`# TYPE ${name} gauge`);
        lines.push(`${name}${lbl} ${value}`);
    };
    gauge('colibri_zkproof_orchestrator_up', 'Orchestrator liveness (1 while running).', 1);
    gauge('colibri_zkproof_orchestrator_last_scan_timestamp_seconds', 'Unix time of the most recent volume scan.', Math.floor(Date.now() / 1000));
    if (m.lastProofMtimeMs !== null) {
        gauge('colibri_zkproof_orchestrator_last_proof_timestamp_seconds', 'Modification time of the most recent client-facing zk_proof.ssz.', Math.floor(m.lastProofMtimeMs / 1000));
        gauge('colibri_zkproof_orchestrator_last_proof_period', 'Highest period that has a built zk_proof.ssz.', m.lastProofPeriod);
    }
    if (m.nextPeriod !== null) {
        gauge('colibri_zkproof_orchestrator_next_period', 'Lowest period that has sync.ssz but no proof yet.', m.nextPeriod);
        if (m.nextDeadlineMs !== null) {
            gauge('colibri_zkproof_orchestrator_next_deadline_timestamp_seconds', 'Unix time by which the next missing proof must exist (start of that period).', Math.floor(m.nextDeadlineMs / 1000));
        }
    }
    const body = lines.join('\n') + '\n';
    const tmp = `${METRICS_FILE}.tmp`;
    try {
        await fsp.writeFile(tmp, body);
        await fsp.rename(tmp, METRICS_FILE);
    } catch (e) {
        warn(`failed to write metrics file ${METRICS_FILE}: ${e.message}`);
    }
}

// --- Job upload / download -------------------------------------------------

function jobPrefix(period) {
    return `jobs/${CHAIN}/${period}/`;
}

/**
 * All S3 keys a job may create on the network volume. Used by cleanupJobDir
 * to wipe a job scratch dir without relying on ListObjectsV2 (which RunPod's
 * S3 gateway rejects with "Invalid object path" for prefixes).
 */
function jobKeys(period) {
    const p = jobPrefix(period);
    return [
        `${p}in/sync.ssz`,
        `${p}in/prev_zk_proof.bin`,
        `${p}in/prev_zk_vk_raw.bin`,
        `${p}out/DONE`,
        `${p}out/FAILED`,
        `${p}out/pod.log`,
        ...ARTIFACTS.map((f) => `${p}out/${f}`),
    ];
}

async function uploadInputs(period, prev) {
    const p = jobPrefix(period);
    const dir = path.join(OUTPUT_DIR, String(period));
    const prevDir = path.join(OUTPUT_DIR, String(prev));

    log(`uploading inputs for period ${period} to s3://${NETWORK_VOLUME_ID}/${p}in/`);
    await s3UploadFile(path.join(dir, 'sync.ssz'),          `${p}in/sync.ssz`);
    await s3UploadFile(path.join(prevDir, 'zk_proof.bin'),  `${p}in/prev_zk_proof.bin`);
    await s3UploadFile(path.join(prevDir, 'zk_vk_raw.bin'), `${p}in/prev_zk_vk_raw.bin`);
}

/**
 * Verify a Groth16 proof with the bundled v5 verifier CLI. Resolves on a valid
 * proof (CLI exit 0) and rejects otherwise, so callers can fail-closed. The CLI
 * signature is `verify_zk_proof_cli <proof_file> <public_values_file>`; a
 * missing binary (ENOENT) is surfaced as a distinct, actionable error so it is
 * never silently treated as "invalid proof".
 */
function verifyProofFiles(proofPath, pubPath) {
    return new Promise((resolve, reject) => {
        execFile(VERIFIER_BIN, [proofPath, pubPath], { timeout: VERIFY_TIMEOUT_MS }, (error, stdout, stderr) => {
            if (!error) return resolve();
            if (error.code === 'ENOENT') {
                return reject(new Error(`verifier binary not found at ${VERIFIER_BIN} (set VERIFIER_BIN, or VERIFY_PROOF=0 to skip verification)`));
            }
            if (error.killed || error.signal) {
                return reject(new Error(`verifier ${VERIFIER_BIN} timed out or was killed (signal ${error.signal || 'n/a'})`));
            }
            const detail = (stderr || stdout || '').toString().trim().split('\n').slice(-3).join(' | ');
            return reject(new Error(`groth16 proof rejected by ${VERIFIER_BIN}${detail ? `: ${detail}` : ''}`));
        });
    });
}

/**
 * Download all published artifacts for `period` from the network volume and
 * atomically publish them into OUTPUT_DIR/<period>/.
 *
 * We use a two-phase publish so that either all mandatory outputs land under
 * OUTPUT_DIR/<period>/ together, or none of them do:
 *   1. Download everything into `<period>/.staging/`.
 *   2. Verify the mandatory artifacts are present.
 *   3. Rename them into place with `zk_proof_g16.bin` last, because
 *      `findMissingPeriod()` uses that file's presence as the "done" signal.
 *      Only after every other artifact is in place is the period visible as
 *      completed.
 */
async function downloadArtifacts(period) {
    const p = jobPrefix(period);
    const dir = path.join(OUTPUT_DIR, String(period));
    const staging = path.join(dir, '.staging');

    await fsp.mkdir(dir, { recursive: true });
    // Fresh staging dir every attempt; a previous partial download must not
    // pollute the publish step below.
    await fsp.rm(staging, { recursive: true, force: true });
    await fsp.mkdir(staging, { recursive: true });

    const staged = [];
    for (const f of ARTIFACTS) {
        const key = `${p}out/${f}`;
        const stagedPath = path.join(staging, f);
        // Download directly and treat a missing key as "not produced". We avoid
        // a separate existence probe on purpose: RunPod's S3 HeadObject is
        // unreliable (see s3ObjectExists) and a GET probe would just download
        // the object twice.
        try {
            await s3DownloadFile(key, stagedPath);
            staged.push(f);
        } catch (e) {
            const status = e && e.$metadata?.httpStatusCode;
            if (status === 404 || e?.name === 'NoSuchKey' || e?.Code === 'NoSuchKey') {
                warn(`artifact ${key} not present on network volume`);
                await fsp.rm(stagedPath, { force: true });
                continue;
            }
            throw e;
        }
    }

    for (const f of ['zk_proof_g16.bin', 'zk_pub.bin']) {
        if (!(await isNonEmpty(path.join(staging, f)))) {
            throw new Error(`mandatory artifact ${f} missing or empty after staged download`);
        }
    }

    // Verify the staged proof before publishing it. Throwing here leaves the
    // staged files in `.staging/` (wiped at the start of the next attempt) and
    // aborts the publish, so a proof the built-in v5 verifier rejects never
    // becomes visible to the prover-service (which would otherwise package it
    // into zk_proof.ssz and serve it to v5 clients).
    //
    // NOTE: this checks the Groth16 proof's *cryptographic validity* against the
    // embedded v5 verification key and sha256(public_inputs). It does not by
    // itself bind the proof to this specific period - that semantic check
    // happens downstream when the client verifies zk_proof.ssz against its own
    // sync-committee state. So this gate is defense-in-depth, not a full
    // period-correctness proof.
    if (VERIFY_PROOF) {
        log(`verifying groth16 proof for period ${period} with ${VERIFIER_BIN}`);
        await verifyProofFiles(path.join(staging, 'zk_proof_g16.bin'), path.join(staging, 'zk_pub.bin'));
        log(`groth16 proof for period ${period} verified OK`);
    } else {
        warn(`publishing period ${period} WITHOUT proof verification (VERIFY_PROOF is off)`);
    }

    // Publish everything except the trigger file first, then rename the
    // trigger file last so the "done" signal is only visible when every
    // other artifact is already in place.
    const trigger = 'zk_proof_g16.bin';
    for (const f of staged) {
        if (f === trigger) continue;
        await fsp.rename(path.join(staging, f), path.join(dir, f));
        log(`installed ${dir}/${f}`);
    }
    if (staged.includes(trigger)) {
        await fsp.rename(path.join(staging, trigger), path.join(dir, trigger));
        log(`installed ${dir}/${trigger}`);
    }
    await fsp.rm(staging, { recursive: true, force: true }).catch(() => {});
}

async function cleanupJobDir(period) {
    // Delete the known keys directly; RunPod's S3 does not support prefix
    // listing, and a stale DONE/FAILED marker from a previous attempt would
    // otherwise make pollJob return immediately with the wrong outcome.
    await Promise.all(jobKeys(period).map((k) => s3DeleteKey(k)));
}

// --- RunPod REST API --------------------------------------------------------

async function runpod(method, apiPath, body) {
    const url = `${RUNPOD_REST}${apiPath}`;
    const res = await fetch(url, {
        method,
        headers: {
            'Authorization': `Bearer ${RUNPOD_API_KEY}`,
            'Content-Type': 'application/json',
        },
        body: body ? JSON.stringify(body) : undefined,
    });
    const text = await res.text();
    if (!res.ok) {
        // Try to extract a useful field from a JSON body; fall back to a
        // truncated snippet for non-JSON responses (e.g. HTML error pages).
        let detail = text.slice(0, 200);
        try {
            const j = JSON.parse(text);
            detail = j.error?.message || j.message || j.detail || detail;
        } catch { /* keep truncated body */ }
        throw new Error(`RunPod ${method} ${apiPath} failed: HTTP ${res.status} ${detail}`);
    }
    if (!text) return null;
    try {
        return JSON.parse(text);
    } catch {
        warn(`RunPod ${method} ${apiPath} returned non-JSON body: ${text.slice(0, 200)}`);
        return text;
    }
}

async function createPod(period) {
    const name = `zkproof-${CHAIN}-${period}-${Math.floor(Date.now() / 1000)}`;
    // The RunPod pod definition schema uses `imageName`, `gpuTypeIds`, plus
    // network-volume attachment. See docs.runpod.io/api-reference/pods.
    const body = {
        name,
        imageName: RUNPOD_IMAGE,
        gpuTypeIds: RUNPOD_GPU_TYPES,
        gpuCount: 1,
        networkVolumeId: NETWORK_VOLUME_ID,
        volumeMountPath: '/workspace',
        containerDiskInGb: CONTAINER_DISK_GB,
        env: {
            CHAIN,
            PERIOD: String(period),
        },
    };
    log(`creating pod ${name} for period ${period} (image=${RUNPOD_IMAGE}, gpus=${RUNPOD_GPU_TYPES.join('|')})`);
    const pod = await runpod('POST', '/pods', body);
    if (!pod || !pod.id) {
        throw new Error(`RunPod pod create returned no id: ${JSON.stringify(pod)}`);
    }
    log(`pod created: id=${pod.id}`);
    return pod.id;
}

async function getPod(podId) {
    return runpod('GET', `/pods/${podId}`);
}

async function terminatePod(podId) {
    // DELETE is the RunPod "terminate" operation for on-demand pods.
    try {
        await runpod('DELETE', `/pods/${podId}`);
        log(`pod ${podId} terminated`);
    } catch (e) {
        warn(`terminate pod ${podId} failed: ${e.message}`);
    }
}

// --- Job polling -----------------------------------------------------------

// The RunPod REST v1 pod schema exposes exactly one status field
// (`desiredStatus`) with the enum {RUNNING, EXITED, TERMINATED}. Only
// RUNNING means the pod is still doing work; the other two are terminal.
// (Older docs / GraphQL API had additional intermediate values, but they
// are not returned by /v1/pods).
const POD_STATUS_LIVE = 'RUNNING';

/**
 * Poll the network volume + pod status until either:
 *   - DONE marker appears (returns 'done'),
 *   - FAILED marker appears (returns 'failed'),
 *   - the pod exits without DONE (returns 'exited'),
 *   - or JOB_TIMEOUT_MS elapses (returns 'timeout').
 */
async function pollJob(period, podId) {
    const deadline = Date.now() + JOB_TIMEOUT_MS;
    const p = jobPrefix(period);
    const doneKey = `${p}out/DONE`;
    const failedKey = `${p}out/FAILED`;

    while (Date.now() < deadline) {
        if (await s3ObjectExists(doneKey)) return 'done';
        if (await s3ObjectExists(failedKey)) return 'failed';

        // A pod that reaches EXITED/TERMINATED without producing DONE has to
        // be treated as a failure so we do not wait for the full timeout.
        let pod;
        try {
            pod = await getPod(podId);
        } catch (e) {
            warn(`polling pod ${podId} failed: ${e.message}`);
        }
        if (pod && pod.desiredStatus && String(pod.desiredStatus).toUpperCase() !== POD_STATUS_LIVE) {
            // Re-check DONE/FAILED once in case the artifacts landed in the
            // same instant the pod exited.
            if (await s3ObjectExists(doneKey)) return 'done';
            if (await s3ObjectExists(failedKey)) return 'failed';
            warn(`pod ${podId} reached terminal status desiredStatus=${pod.desiredStatus} without writing DONE`);
            return 'exited';
        }

        await sleep(POLL_INTERVAL_MS);
    }
    return 'timeout';
}

function sleep(ms) {
    return new Promise((r) => setTimeout(r, ms));
}

// --- Active pod tracking (for SIGTERM cleanup) -----------------------------

let activePodId = null;
let shuttingDown = false;

async function setActivePod(id) {
    activePodId = id;
    // If we were told to shut down while the pod was being created, terminate
    // it immediately.
    if (shuttingDown && activePodId) {
        await terminatePod(activePodId).catch(() => {});
        activePodId = null;
    }
}

for (const sig of ['SIGTERM', 'SIGINT']) {
    process.on(sig, () => {
        if (shuttingDown) return;
        shuttingDown = true;
        warn(`received ${sig}: terminating active pod ${activePodId || '(none)'} before exit`);
        (async () => {
            if (activePodId) {
                await terminatePod(activePodId).catch(() => {});
                activePodId = null;
            }
            process.exit(0);
        })();
    });
}

// --- One iteration ---------------------------------------------------------

/**
 * Try to process exactly one missing period. Returns:
 *   - true if a period was fully proved AND the artifacts landed under
 *     OUTPUT_DIR/<period>/ (safe to look for the next missing period),
 *   - false otherwise (no work / partial failure / pod terminated without
 *     DONE / etc.). Callers must NOT retry immediately on false - the tick
 *     loop reschedules via setInterval so the operator gets time to react.
 */
async function processOne() {
    if (shuttingDown) return false;

    const missing = await findMissingPeriod();
    if (!missing) {
        log('no periods missing zk_proof_g16.bin - idle');
        return false;
    }
    const { period, prev, deadlineMs } = missing;
    const dueStr = deadlineMs !== null
        ? `proof due ${new Date(deadlineMs).toISOString()} (start of period ${period})`
        : 'no deadline table for this chain';
    log(`selected period ${period} (prev ${prev}) for proving on RunPod - ${dueStr}`);

    // Fresh scratch dir on the volume - remove any leftovers from a previous
    // failed attempt so the pod does not see stale DONE/FAILED markers.
    await cleanupJobDir(period);

    try {
        await uploadInputs(period, prev);
    } catch (e) {
        err(`uploadInputs failed for period ${period}: ${e.message}`);
        return false;
    }

    let podId;
    try {
        podId = await createPod(period);
        await setActivePod(podId);
    } catch (e) {
        err(`createPod failed for period ${period}: ${e.message}`);
        return false;
    }

    let outcome = 'timeout';
    try {
        outcome = await pollJob(period, podId);
    } catch (e) {
        err(`pollJob failed for period ${period}: ${e.message}`);
        outcome = 'error';
    } finally {
        // Always tear the pod down: on success (job is done and we do not
        // want to burn GPU credits idling) and on failure (avoid orphans).
        await terminatePod(podId);
        if (activePodId === podId) activePodId = null;
    }

    if (outcome !== 'done') {
        err(`period ${period} did not succeed (outcome=${outcome}); backing off to the next interval, leaving network-volume dir for inspection`);
        return false;
    }

    try {
        await downloadArtifacts(period);
    } catch (e) {
        err(`downloadArtifacts failed for period ${period}: ${e.message}`);
        return false;
    }

    await cleanupJobDir(period);
    log(`period ${period} completed successfully`);
    return true;
}

// --- Main loop -------------------------------------------------------------

async function main() {
    log('starting orchestrator');
    log(`  chain=${CHAIN}`);
    log(`  volume=${NETWORK_VOLUME_ID} datacenter=${RUNPOD_DATACENTER}`);
    log(`  image=${RUNPOD_IMAGE} gpus=${RUNPOD_GPU_TYPES.join('|')}`);
    log(`  output_dir=${OUTPUT_DIR}`);
    log(`  check_interval=${CHECK_INTERVAL_MS}ms job_timeout=${JOB_TIMEOUT_MS}ms`);
    const timingKnown = !!CHAIN_TIMING[CHAIN.toLowerCase()];
    log(`  prove_lead=${PROVE_LEAD_MS}ms deadline_gating=${PROVE_LEAD_MS > 0 && timingKnown ? 'on' : 'off'}${PROVE_LEAD_MS > 0 && !timingKnown ? ` (no timing table for chain '${CHAIN}')` : ''}`);
    log(`  s3_endpoint=${S3_ENDPOINT} region=${S3_REGION}`);
    log(`  metrics_file=${METRICS_FILE || '(disabled)'}`);
    log(`  verify_proof=${VERIFY_PROOF ? `on (${VERIFIER_BIN})` : 'off'}`);
    log(`  hostname=${os.hostname()}`);

    let running = false;
    const tick = async () => {
        if (running) {
            log('previous tick still running, skipping');
            return;
        }
        if (shuttingDown) return;
        running = true;
        try {
            // Process periods bottom-up until either everything is caught up
            // (`processOne` returns false with "no periods missing") or the
            // current period failed (also returns false). Either way we stop
            // this tick and wait for the next interval so operators get a
            // chance to react to persistent errors before we spend more GPU
            // credits.
            while (await processOne()) {
                // loop
            }
        } catch (e) {
            err(`tick failed: ${e.stack || e.message}`);
        } finally {
            // Refresh the Prometheus textfile after every tick so the "next
            // deadline" and "last proof" gauges reflect the post-tick state.
            await writeMetricsFile();
            running = false;
        }
    };

    // Initial tick, then interval.
    await tick();
    setInterval(tick, CHECK_INTERVAL_MS);
}

main().catch((e) => {
    err(`fatal: ${e.stack || e.message}`);
    process.exit(1);
});
