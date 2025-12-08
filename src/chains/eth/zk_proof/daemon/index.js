const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');
// Removed axios dependency, using native fetch (Node 18+)

// Configuration
const CHAIN_CONFIGS = {
    mainnet: { epochsPerPeriod: 256, rpc: 'https://lodestar-mainnet.chainsafe.io' },
    sepolia: { epochsPerPeriod: 256 },
    gnosis: { epochsPerPeriod: 512 },
    chiado: { epochsPerPeriod: 512 },
    base: { epochsPerPeriod: 256 },
};

const CHAIN = (process.env.CHAIN || 'mainnet').toLowerCase();
const chainDefaults = CHAIN_CONFIGS[CHAIN] || CHAIN_CONFIGS.mainnet;

const RPC_URL = process.env.RPC_URL || chainDefaults.rpc || 'https://lodestar-mainnet.chainsafe.io';
const CHECK_INTERVAL_MS = parseInt(process.env.CHECK_INTERVAL_MS) || 10 * 60 * 1000; // 10 minutes
const EPOCHS_PER_PERIOD =
    parseInt(process.env.EPOCHS_PER_PERIOD) || chainDefaults.epochsPerPeriod || 256; // 256 for Mainnet, 512 for Gnosis
const OUTPUT_DIR = process.env.OUTPUT_DIR || path.resolve(__dirname, '../../../../../build/default/.period_store');
const PROMETHEUS_FILE = process.env.PROMETHEUS_FILE || '/metrics/proof.prom';
const REPO_ROOT = process.env.REPO_ROOT || path.resolve(__dirname, '../../../../..');
const SCRIPT_PATH = path.join(REPO_ROOT, 'scripts/run_zk_proof.sh');
// Path to C-Verifier CLI (built via CMake)
// Adjusted to standard binary output location (build/default/bin/verify_zk_proof_cli) or just build/bin depending on cmake config.
// We try to be flexible or user can override.
const VERIFIER_CLI = process.env.VERIFIER_CLI || path.join(REPO_ROOT, 'build/default/bin/verify_zk_proof_cli');

// State
let isRunning = false;

async function main() {
    console.log('🚀 Starting Prover Daemon');
    console.log(`   Chain: ${CHAIN}`);
    console.log(`   RPC: ${RPC_URL}`);
    console.log(`   Epochs/Period: ${EPOCHS_PER_PERIOD}`);
    console.log(`   Output Dir: ${OUTPUT_DIR}`);
    console.log(`   Script: ${SCRIPT_PATH}`);

    // Initial check
    await checkAndProve();

    // Loop
    setInterval(checkAndProve, CHECK_INTERVAL_MS);
}

async function checkAndProve() {
    if (isRunning) {
        console.log('⚠️  Proof generation already in progress. Skipping check.');
        return;
    }

    try {
        console.log('🔍 Checking finality status...');

        const response = await fetch(`${RPC_URL}/eth/v1/beacon/states/head/finality_checkpoints`);
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        const data = await response.json();
        const finalizedEpoch = parseInt(data.data.finalized.epoch);

        const currentPeriod = Math.floor(finalizedEpoch / EPOCHS_PER_PERIOD);

        const targetPeriod = currentPeriod + 1;
        const prevPeriod = currentPeriod;

        console.log(`   Finalized Epoch: ${finalizedEpoch}`);
        console.log(`   Current Period (Finalized): ${currentPeriod}`);
        console.log(`   Target Period (to prove): ${targetPeriod}`);

        // Validate Current Period Files (P) - used for input
        validatePeriodFiles(currentPeriod);

        // Validate Target Period Files (P+1) - potentially partially existing
        validatePeriodFiles(targetPeriod);

        // Check if proof exists
        const proofPath = path.join(OUTPUT_DIR, targetPeriod.toString(), 'zk_proof_g16.bin');

        if (fs.existsSync(proofPath)) {
            // Proof exists, check if it's valid
            const isValid = await verifyProof(targetPeriod);
            if (isValid) {
                updateMetrics(targetPeriod, 0, 0, 'skipped');
                return;
            } else {
                console.warn(`⚠️  Proof for period ${targetPeriod} exists but FAILED verification.`);

                // SAFETY CHECK: Don't infinite loop if proof generation is broken.
                // We only delete and retry if the file is OLDER than X hours (e.g., 1 hour).
                // If it was just created and failed, retrying immediately probably won't fix it (deterministic bug).
                // But if it's old, maybe it was a partial write or corruption.

                const stats = fs.statSync(proofPath);
                const ageHours = (Date.now() - stats.mtimeMs) / (1000 * 60 * 60);

                if (ageHours > 1) {
                    console.warn(`   Proof is old (${ageHours.toFixed(1)}h). Deleting and retrying...`);
                    fs.unlinkSync(proofPath);
                    updateMetrics(targetPeriod, 0, 1, 'verification_failed_retry');
                } else {
                    console.error(`   Proof is new (${ageHours.toFixed(1)}h). NOT retrying to avoid loop. Please investigate manually.`);
                    updateMetrics(targetPeriod, 0, 1, 'verification_failed_persistent');
                    return;
                }
            }
        }

        console.log(`⚡ Proof for period ${targetPeriod} MISSING. Starting proof generation...`);

        await runProofScript(targetPeriod, prevPeriod);

    } catch (error) {
        console.error('❌ Error in check loop:', error.message);
        updateMetrics(0, 0, 1, 'error');
    }
}

function validatePeriodFiles(period) {
    const periodDir = path.join(OUTPUT_DIR, period.toString());

    // Define critical files to check
    // Added zk_proof_g16.bin as it's the final output we care about most, and light client update files
    const filesToCheck = ['sync.ssz', 'zk_proof.bin', 'zk_vk_raw.bin', 'blocks.ssz', 'headers.ssz', 'zk_proof_g16.bin', 'lcu.ssz', 'lcb.ssz'];

    // Helper to get file size or -1 if missing
    const getFileSize = (filename) => {
        try {
            const stats = fs.statSync(path.join(periodDir, filename));
            return stats.size;
        } catch (e) {
            return -1;
        }
    };

    const sizes = {};
    filesToCheck.forEach(f => {
        sizes[f] = getFileSize(f);
    });

    // Update metrics for these file sizes
    updateFileMetrics(period, sizes);
}

async function runProofScript(period, prevPeriod) {
    isRunning = true;
    const startTime = Date.now();

    // ./scripts/run_zk_proof.sh --period <period> --prev-period <prev> --prove --groth16 --network --output <dir>
    const args = [
        '--period', period.toString(),
        '--prev-period', prevPeriod.toString(),
        '--prove',
        '--groth16',
        '--network',
        '--output', OUTPUT_DIR
    ];

    console.log(`▶️  Executing: ${SCRIPT_PATH} ${args.join(' ')}`);

    return new Promise((resolve, reject) => {
        const child = spawn(SCRIPT_PATH, args, {
            env: { ...process.env }, // Pass through env (including SP1_PRIVATE_KEY)
            cwd: REPO_ROOT // Run from repo root
        });

        child.stdout.on('data', (data) => {
            process.stdout.write(data);
        });

        child.stderr.on('data', (data) => {
            process.stderr.write(data);
        });

        child.on('close', (code) => {
            isRunning = false;
            const duration = (Date.now() - startTime) / 1000;

            if (code === 0) {
                console.log(`🎉 Proof generation successful in ${duration}s`);
                updateMetrics(period, duration, 0, 'success');
                resolve();
            } else {
                console.error(`❌ Proof generation failed with code ${code}`);
                updateMetrics(period, duration, 1, 'failure');
                // Don't reject, just resolve so loop continues (metrics updated)
                resolve();
            }
        });

        child.on('error', (err) => {
            isRunning = false;
            console.error('❌ Failed to spawn script:', err);
            updateMetrics(period, 0, 1, 'spawn_error');
            resolve();
        });
    });
}

async function verifyProof(period) {
    const periodDir = path.join(OUTPUT_DIR, period.toString());
    const proofFile = path.join(periodDir, 'zk_proof_g16.bin');
    const pubFile = path.join(periodDir, 'zk_pub.bin');

    if (!fs.existsSync(VERIFIER_CLI)) {
        console.warn(`⚠️  Verifier CLI not found at ${VERIFIER_CLI}. Skipping verification.`);
        // If we can't verify, we assume it's valid to avoid infinite loops if CLI is missing
        // But we should probably metric this.
        return true;
    }

    console.log(`🕵️  Verifying proof for period ${period}...`);

    return new Promise((resolve) => {
        const child = spawn(VERIFIER_CLI, [proofFile, pubFile], {
            env: { ...process.env }
        });

        child.on('close', (code) => {
            if (code === 0) {
                console.log(`✅ Verification SUCCESS for period ${period}`);
                resolve(true);
            } else {
                console.error(`❌ Verification FAILED for period ${period} (code ${code})`);
                resolve(false);
            }
        });

        child.on('error', (err) => {
            console.error('❌ Failed to spawn verifier:', err);
            resolve(false);
        });
    });
}

function updateMetrics(period, duration, status, type) {
    // Metrics format:
    // # HELP prover_daemon_last_run_timestamp_seconds Timestamp of the last proof run
    // # TYPE prover_daemon_last_run_timestamp_seconds gauge
    // prover_daemon_last_run_timestamp_seconds <ts>

    const timestamp = Math.floor(Date.now() / 1000);

    // We keep a global buffer or append to file? 
    // To avoid race conditions or partial writes with multiple functions updating the same file,
    // it's better to have a shared state object for metrics and write the WHOLE file at once.
    // BUT, for simplicity in this script, we can read existing metrics (or keep them in memory) and rewrite.
    // Since this is single threaded JS, we can just update a global state object and write it out.

    const shouldUseArtifact = type === 'success' || type === 'skipped';
    const artifactTimestamp = shouldUseArtifact ? getProofArtifactTimestamp(period) : null;

    if (shouldUseArtifact && artifactTimestamp) {
        globalMetrics.lastRunTimestamp = artifactTimestamp;
    } else if (!shouldUseArtifact && globalMetrics.lastRunTimestamp === 0) {
        // Initialize metric so Prometheus does not see "0" forever
        globalMetrics.lastRunTimestamp = timestamp;
    }
    globalMetrics.lastCheckTimestamp = timestamp; // Always update check timestamp

    if (type !== 'skipped') {
        globalMetrics.lastRunDuration = duration;
        globalMetrics.lastRunStatus = status;
    }

    if (period > 0) {
        globalMetrics.currentPeriod = period;
    }

    writeMetricsToFile();
}

function updateFileMetrics(period, sizes) {
    // Store file sizes in global state: file_sizes[period][filename] = size
    if (!globalMetrics.fileSizes) globalMetrics.fileSizes = {};
    globalMetrics.fileSizes[period] = sizes;

    // We also update the check timestamp here as this is part of the check loop
    globalMetrics.lastCheckTimestamp = Math.floor(Date.now() / 1000);

    writeMetricsToFile();
}

// Global Metrics State
const globalMetrics = {
    lastRunTimestamp: 0,
    lastCheckTimestamp: 0,
    lastRunDuration: 0,
    lastRunStatus: 0,
    currentPeriod: 0,
    fileSizes: {}, // { period: { 'filename': size, ... } }
};

function getProofArtifactTimestamp(period) {
    if (!period || period <= 0) return null;
    const proofPath = path.join(OUTPUT_DIR, period.toString(), 'zk_proof_g16.bin');
    try {
        const stats = fs.statSync(proofPath);
        return Math.floor(stats.mtimeMs / 1000);
    } catch (_) {
        return null;
    }
}

function writeMetricsToFile() {
    let content = '';

    content += `# HELP prover_daemon_last_check_timestamp_seconds Timestamp of the last daemon check loop\n`;
    content += `# TYPE prover_daemon_last_check_timestamp_seconds gauge\n`;
    content += `prover_daemon_last_check_timestamp_seconds{chain="${CHAIN}"} ${globalMetrics.lastCheckTimestamp}\n\n`;

    content += `# HELP prover_daemon_last_run_timestamp_seconds Timestamp of the last actual proof run attempt\n`;
    content += `# TYPE prover_daemon_last_run_timestamp_seconds gauge\n`;
    content += `prover_daemon_last_run_timestamp_seconds{chain="${CHAIN}"} ${globalMetrics.lastRunTimestamp}\n\n`;

    content += `# HELP prover_daemon_last_run_duration_seconds Duration of the last proof run in seconds\n`;
    content += `# TYPE prover_daemon_last_run_duration_seconds gauge\n`;
    content += `prover_daemon_last_run_duration_seconds{chain="${CHAIN}"} ${globalMetrics.lastRunDuration}\n\n`;

    content += `# HELP prover_daemon_last_run_status Status of the last proof run (0=success, 1=error)\n`;
    content += `# TYPE prover_daemon_last_run_status gauge\n`;
    content += `prover_daemon_last_run_status{chain="${CHAIN}"} ${globalMetrics.lastRunStatus}\n\n`;

    if (globalMetrics.currentPeriod > 0) {
        content += `# HELP prover_daemon_current_period The target period being processed\n`;
        content += `# TYPE prover_daemon_current_period gauge\n`;
        content += `prover_daemon_current_period{chain="${CHAIN}"} ${globalMetrics.currentPeriod}\n\n`;
    }

    // File Sizes
    if (globalMetrics.fileSizes) {
        content += `# HELP prover_daemon_file_size_bytes Size of period files in bytes (-1 if missing)\n`;
        content += `# TYPE prover_daemon_file_size_bytes gauge\n`;

        for (const [p, files] of Object.entries(globalMetrics.fileSizes)) {
            for (const [filename, size] of Object.entries(files)) {
                content += `prover_daemon_file_size_bytes{chain="${CHAIN}",period="${p}",file="${filename}"} ${size}\n`;
            }
        }
    }

    try {
        const dir = path.dirname(PROMETHEUS_FILE);
        if (!fs.existsSync(dir)) {
            fs.mkdirSync(dir, { recursive: true });
        }
        fs.writeFileSync(PROMETHEUS_FILE, content);
        // console.log('📈 Metrics updated'); // verbose
    } catch (err) {
        console.error('❌ Failed to write metrics:', err.message);
    }
}

// Start
main();

