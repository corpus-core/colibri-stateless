#!/usr/bin/env bash
#
# entrypoint.sh - build one v5 Groth16 proof on a RunPod GPU pod.
#
# Contract with the orchestrator (see runpod/orchestrator/orchestrator.js):
#
#   Inputs on the RunPod network volume (mounted at /workspace):
#     /workspace/jobs/<CHAIN>/<PERIOD>/in/sync.ssz              (mandatory)
#     /workspace/jobs/<CHAIN>/<PERIOD>/in/prev_zk_proof.bin     (mandatory)
#     /workspace/jobs/<CHAIN>/<PERIOD>/in/prev_zk_vk_raw.bin    (mandatory)
#
#   Outputs on success:
#     /workspace/jobs/<CHAIN>/<PERIOD>/out/{zk_proof_g16.bin,
#                                          zk_pub.bin,
#                                          zk_proof.bin,
#                                          zk_vk_raw.bin,
#                                          zk_groth16.bin,
#                                          zk_vk.bin}
#     /workspace/jobs/<CHAIN>/<PERIOD>/out/DONE
#
#   Marker on failure (non-zero exit, orchestrator will terminate the pod):
#     /workspace/jobs/<CHAIN>/<PERIOD>/out/FAILED
#
# Env:
#   CHAIN   - mainnet | sepolia | gnosis | chiado | base (label only here)
#   PERIOD  - target period to prove (u64)
#   MOONGATE_PORT (default 3000)  - port moongate-server listens on
#   RUNPOD_POD_ID (optional, informational)
#
set -euo pipefail

log() { printf '[entrypoint %s] %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*"; }
die() { log "FATAL: $*"; exit 1; }

: "${CHAIN:?CHAIN env is required}"
: "${PERIOD:?PERIOD env is required}"
: "${MOONGATE_PORT:=3000}"

# Basic sanity: period must be numeric.
case "${PERIOD}" in
    ''|*[!0-9]*) die "PERIOD must be a positive integer, got '${PERIOD}'";;
esac

PREV="$((PERIOD - 1))"

WORK="/workspace/jobs/${CHAIN}/${PERIOD}"
IN_DIR="${WORK}/in"
OUT_DIR="${WORK}/out"
mkdir -p "${OUT_DIR}"

# Persist all stdout/stderr onto the network volume so a failed run stays
# diagnosable after the ephemeral pod is terminated. The orchestrator can pull
# ${OUT_DIR}/pod.log via the S3 API. `tee` keeps the RunPod console log intact.
exec > >(tee -a "${OUT_DIR}/pod.log") 2>&1

# From here on, any error path writes the FAILED marker before exiting.
mark_failed() {
    log "writing FAILED marker to ${OUT_DIR}/FAILED"
    : > "${OUT_DIR}/FAILED" || true
}
trap 'rc=$?; if [ "$rc" -ne 0 ]; then mark_failed; fi' EXIT

# Ensure the FAILED marker from a previous attempt is not confused with the
# current one. DONE is only ever written at the very end of a successful run.
rm -f "${OUT_DIR}/FAILED" "${OUT_DIR}/DONE"

log "chain=${CHAIN} period=${PERIOD} prev=${PREV} moongate_port=${MOONGATE_PORT}"

# --- 1. Verify inputs ------------------------------------------------------
[ -s "${IN_DIR}/sync.ssz" ]          || die "missing input: ${IN_DIR}/sync.ssz"
[ -s "${IN_DIR}/prev_zk_proof.bin" ] || die "missing input: ${IN_DIR}/prev_zk_proof.bin (recursion is mandatory)"
[ -s "${IN_DIR}/prev_zk_vk_raw.bin" ] || die "missing input: ${IN_DIR}/prev_zk_vk_raw.bin"

# --- 2. Build the .period_store layout expected by run_zk_proof.sh ---------
# The script writes/reads via `<output>/<PERIOD>/...`, and picks up the
# previous period's recursion inputs from `<output>/<PREV>/{zk_proof.bin,
# zk_vk_raw.bin}`.
#
# We keep the store inside /workspace so it survives a pod restart (cheap
# retries) but is scoped to this job so parallel jobs cannot collide.
STORE="${WORK}/store"
mkdir -p "${STORE}/${PERIOD}" "${STORE}/${PREV}"
cp -f "${IN_DIR}/sync.ssz"           "${STORE}/${PERIOD}/sync.ssz"
cp -f "${IN_DIR}/prev_zk_proof.bin"  "${STORE}/${PREV}/zk_proof.bin"
cp -f "${IN_DIR}/prev_zk_vk_raw.bin" "${STORE}/${PREV}/zk_vk_raw.bin"

# --- 3. Share the Groth16 circuit cache across pods via the volume ---------
# sp1-sdk installs the Groth16 v5.0.0 circuit (~4 GB) under $HOME/.sp1/circuits
# on first use. Symlinking that directory into /workspace makes the download a
# one-time cost across every pod using the same network volume.
mkdir -p /workspace/.sp1/circuits "${HOME}/.sp1"
ln -sfn /workspace/.sp1/circuits "${HOME}/.sp1/circuits"

# --- 4. Start moongate-server ---------------------------------------------
# We start moongate natively so the SP1 SDK does not try to spawn a Docker
# container (which would fail inside a RunPod pod without docker-in-docker).
# main.rs picks up SP1_MOONGATE_ENDPOINT and calls
# `ProverClient::builder().cuda().server(...)`.
log "starting moongate-server on 127.0.0.1:${MOONGATE_PORT}"
: > /tmp/moongate.log
moongate-server >/tmp/moongate.log 2>&1 &
MOONGATE_PID=$!

# Ensure we always tear moongate down when this script exits (success or fail).
cleanup_moongate() {
    if [ -n "${MOONGATE_PID:-}" ] && kill -0 "${MOONGATE_PID}" 2>/dev/null; then
        log "stopping moongate-server (pid=${MOONGATE_PID})"
        kill "${MOONGATE_PID}" 2>/dev/null || true
        wait "${MOONGATE_PID}" 2>/dev/null || true
    fi
}

# On failure, fold moongate's own log into the captured output (pod.log) so a
# moongate-side problem is diagnosable from the network volume alone.
dump_moongate_log() {
    if [ -f /tmp/moongate.log ]; then
        log "--- moongate-server log (tail) ---"
        tail -n 200 /tmp/moongate.log || true
        log "--- end moongate-server log ---"
    fi
}
trap 'rc=$?; cleanup_moongate; if [ "$rc" -ne 0 ]; then dump_moongate_log; mark_failed; fi' EXIT

# Wait until the port is accepting connections or moongate has died.
READY=0
for _ in $(seq 1 120); do
    if ! kill -0 "${MOONGATE_PID}" 2>/dev/null; then
        log "moongate-server exited during startup, log follows:"
        cat /tmp/moongate.log || true
        die "moongate-server failed to start"
    fi
    # /dev/tcp is a bash-only feature and this script explicitly runs under bash.
    if (exec 3<>/dev/tcp/127.0.0.1/"${MOONGATE_PORT}") 2>/dev/null; then
        exec 3<&- || true
        exec 3>&- || true
        READY=1
        break
    fi
    sleep 1
done
[ "${READY}" -eq 1 ] || { cat /tmp/moongate.log || true; die "moongate-server did not become ready within 120s"; }
log "moongate-server ready"

# --- 5. Run the proof ------------------------------------------------------
# run_zk_proof.sh auto-detects /app/eth_sync_program + /app/eth-sync-script and
# skips the toolchain / rebuild path. It also auto-detects the previous period
# from ${STORE}/${PREV}/ so we do not need to pass --prev-period explicitly.
export SP1_PROVER=cuda
# The endpoint must be the Twirp base URL and end in `/twirp/`: sp1-cuda uses it
# verbatim as the twirp client base (Client::new(Url::parse(endpoint))), and the
# SDK's own docker path uses `http://localhost:<port>/twirp/`. Passing the bare
# `http://host:port` makes every RPC (including the readiness probe) hit the
# wrong path, so CUDA init times out with "proving server did not become ready".
export SP1_MOONGATE_ENDPOINT="http://127.0.0.1:${MOONGATE_PORT}/twirp/"
# The Groth16 wrap is verified locally by the SDK; inside a container we skip
# it (there is no separate Docker verifier available) and rely on the
# orchestrator's verify_zk_proof_cli step on the server side instead.
export SP1_SKIP_VERIFY=1

log "starting run_zk_proof.sh --prove --groth16 --period ${PERIOD} --output ${STORE}"
/app/scripts/run_zk_proof.sh \
    --prove --groth16 \
    --period "${PERIOD}" \
    --output "${STORE}"

# --- 6. Publish outputs ---------------------------------------------------
# Same set that build_proof uploads today (see scripts/build_proof: UPLOAD_FILES)
# and that orchestrator.js pulls back via `ARTIFACTS`.
OUT_FILES=(zk_proof_g16.bin zk_pub.bin zk_proof.bin zk_vk_raw.bin zk_groth16.bin zk_vk.bin)
for f in "${OUT_FILES[@]}"; do
    src="${STORE}/${PERIOD}/${f}"
    if [ -s "${src}" ]; then
        # Atomic publish: write to a temp file, then rename.
        tmp="${OUT_DIR}/.${f}.tmp"
        cp -f "${src}" "${tmp}"
        mv -f "${tmp}" "${OUT_DIR}/${f}"
        log "published ${f} ($(wc -c < "${OUT_DIR}/${f}") bytes)"
    else
        log "warning: expected artifact missing or empty: ${src}"
    fi
done

# The final Groth16 output is mandatory for the orchestrator to succeed.
if [ ! -s "${OUT_DIR}/zk_proof_g16.bin" ]; then
    die "zk_proof_g16.bin was not produced (check run_zk_proof.sh output above)"
fi
if [ ! -s "${OUT_DIR}/zk_pub.bin" ]; then
    die "zk_pub.bin was not produced"
fi

# Success - write the DONE marker last so the orchestrator sees a consistent
# publish state before it starts pulling artifacts back to the server.
: > "${OUT_DIR}/DONE"
log "DONE for period ${PERIOD}"
