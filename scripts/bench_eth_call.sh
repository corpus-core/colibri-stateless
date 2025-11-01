#!/usr/bin/env bash
set -euo pipefail

# Absolute project root (adjust if needed)
ROOT_DIR="/Users/simon/ws/cc/colibri-stateless"
BIN_DIR="$ROOT_DIR/build/default/bin"

SERVER_BIN="$BIN_DIR/colibri-server"
VERIFIER_BIN="$BIN_DIR/colibri-verifier"

# External endpoints
BEACON_URL="http://localhost:5052/"
RPC_URL="http://localhost:8545"

# Colibri server options
SERVER_PORT=8090

# Contract/method for balanceOf(address)
TO_CONTRACT="0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48" # USDC mainnet
METHOD_ID="70a08231" # balanceOf(address)

if [[ ! -x "$SERVER_BIN" || ! -x "$VERIFIER_BIN" ]]; then
  echo "Binaries not found. Build first: (cd $ROOT_DIR/build/default && make -j4)" >&2
  exit 1
fi

echo "Starting server on port $SERVER_PORT ..."
"$SERVER_BIN" -b "$BEACON_URL" -r "$RPC_URL" -e &
SERVER_PID=$!
trap 'echo "Stopping server ($SERVER_PID)"; kill "$SERVER_PID" >/dev/null 2>&1 || true; wait "$SERVER_PID" 2>/dev/null || true' EXIT

echo "Waiting 25s for server warmup/caches ..."
sleep 25

echo "Sending 10 eth_call requests ..."
for i in $(seq 1 10); do
  # Build 20-byte (40 hex chars) address suffix from i
  SUFFIX=$(printf "%040x" "$i")
  # 32-byte encoded argument = 24 zeros (12 bytes) + 20-byte address
  ARG="000000000000000000000000${SUFFIX}"
  DATA="0x${METHOD_ID}${ARG}"

  PAYLOAD=$(printf '{"to":"%s","data":"%s"}' "$TO_CONTRACT" "$DATA")

  "$VERIFIER_BIN" -i "http://localhost:${SERVER_PORT}" eth_call "$PAYLOAD" latest >/dev/null || true
done

echo "Fetching Prometheus metrics ..."
curl -s "http://localhost:${SERVER_PORT}/metrics" | grep colibri_eth_call_prover || true

echo "Stopping server ($SERVER_PID) ..."
kill "$SERVER_PID" >/dev/null 2>&1 || true
wait "$SERVER_PID" 2>/dev/null || true
trap - EXIT

echo "Done."

