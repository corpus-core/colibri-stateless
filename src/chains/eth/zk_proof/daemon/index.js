const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');
// Removed axios dependency, using native fetch (Node 18+)

// Configuration
const RPC_URL = process.env.RPC_URL || 'https://lodestar-mainnet.chainsafe.io';
const CHECK_INTERVAL_MS = parseInt(process.env.CHECK_INTERVAL_MS) || 10 * 60 * 1000; // 10 minutes
const EPOCHS_PER_PERIOD = parseInt(process.env.EPOCHS_PER_PERIOD) || 256; // 256 for Mainnet, 512 for Gnosis
const OUTPUT_DIR = process.env.OUTPUT_DIR || path.resolve(__dirname, '../../../../../build/default/.period_store');
const PROMETHEUS_FILE = process.env.PROMETHEUS_FILE || '/metrics/proof.prom';
const REPO_ROOT = process.env.REPO_ROOT || path.resolve(__dirname, '../../../../..');
const SCRIPT_PATH = path.join(REPO_ROOT, 'scripts/run_zk_proof.sh');

// State
let isRunning = false;

async function main() {
    console.log('🚀 Starting Prover Daemon');
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
        // We always build for the NEXT period (next sync committee) based on the finalized period P.
        // But strictly speaking, the light client update logic usually generates a proof for the transition 
        // from Period P to P+1 ONCE Period P is finalized.
        // The user specified: "wenn ... Period 1606 der letzte finale Block ... liegt, dann bauen wir den proof für 1607"
        // So finalized period = P, target period = P + 1.
        
        const targetPeriod = currentPeriod + 1;
        const prevPeriod = currentPeriod;

        console.log(`   Finalized Epoch: ${finalizedEpoch}`);
        console.log(`   Current Period (Finalized): ${currentPeriod}`);
        console.log(`   Target Period (to prove): ${targetPeriod}`);

        // Check if proof exists
        // Structure: $OUTPUT_DIR/$PERIOD/zk_proof_g16.bin
        // Note: run_zk_proof.sh creates the directory $OUTPUT_DIR/$PERIOD
        const proofPath = path.join(OUTPUT_DIR, targetPeriod.toString(), 'zk_proof_g16.bin');

        if (fs.existsSync(proofPath)) {
            // Proof exists, do nothing.
            // We don't log here to keep logs clean as this runs every 10 mins.
            updateMetrics(targetPeriod, 0, 0, 'skipped');
            return;
        }

        console.log(`⚡ Proof for period ${targetPeriod} MISSING. Starting proof generation...`);
        
        await runProofScript(targetPeriod, prevPeriod);

    } catch (error) {
        console.error('❌ Error in check loop:', error.message);
        updateMetrics(0, 0, 1, 'error');
    }
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

function updateMetrics(period, duration, status, type) {
    // Metrics format:
    // # HELP prover_daemon_last_run_timestamp_seconds Timestamp of the last proof run
    // # TYPE prover_daemon_last_run_timestamp_seconds gauge
    // prover_daemon_last_run_timestamp_seconds <ts>
    
    const timestamp = Math.floor(Date.now() / 1000);
    
    let content = '';
    
    // New metric: Timestamp of the last check (regardless of outcome)
    // This is crucial for liveness monitoring.
    content += `# HELP prover_daemon_last_check_timestamp_seconds Timestamp of the last daemon check loop\n`;
    content += `# TYPE prover_daemon_last_check_timestamp_seconds gauge\n`;
    content += `prover_daemon_last_check_timestamp_seconds ${timestamp}\n\n`;

    if (type !== 'skipped') {
        content += `# HELP prover_daemon_last_run_timestamp_seconds Timestamp of the last actual proof run attempt\n`;
        content += `# TYPE prover_daemon_last_run_timestamp_seconds gauge\n`;
        content += `prover_daemon_last_run_timestamp_seconds ${timestamp}\n\n`;

        content += `# HELP prover_daemon_last_run_duration_seconds Duration of the last proof run in seconds\n`;
        content += `# TYPE prover_daemon_last_run_duration_seconds gauge\n`;
        content += `prover_daemon_last_run_duration_seconds ${duration}\n\n`;

        content += `# HELP prover_daemon_last_run_status Status of the last proof run (0=success, 1=error)\n`;
        content += `# TYPE prover_daemon_last_run_status gauge\n`;
        content += `prover_daemon_last_run_status ${status}\n\n`;
    }
    
    if (period > 0) {
        content += `# HELP prover_daemon_current_period The target period being processed\n`;
        content += `# TYPE prover_daemon_current_period gauge\n`;
        content += `prover_daemon_current_period ${period}\n`;
    }

    try {
        // Ensure directory exists
        const dir = path.dirname(PROMETHEUS_FILE);
        if (!fs.existsSync(dir)) {
            fs.mkdirSync(dir, { recursive: true });
        }
        fs.writeFileSync(PROMETHEUS_FILE, content);
        console.log('📈 Metrics updated');
    } catch (err) {
        console.error('❌ Failed to write metrics:', err.message);
    }
}

// Start
main();

