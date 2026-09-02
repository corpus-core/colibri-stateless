#!/usr/bin/env bash
# Run the live RPC integration suite against a local prover.
# RPC and Beacon API stay on public infrastructure unless C4_RPC_URL /
# C4_BEACON_URL are already set.
#
# Usage:
#   ./test/run_rpc_integration_local.sh
#   C4_MODES=remote C4_PRIVACY=none ./test/run_rpc_integration_local.sh
#   C4_COMPARE=values ./test/run_rpc_integration_local.sh  # fail on extra Colibri keys
#   C4_CHAIN_ID=plataberget ./test/run_rpc_integration_local.sh  # Glamsterdam devnet

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PKG_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

export C4_RUN_RPC_INTEGRATION=1
export C4_PROVER_URL="${C4_PROVER_URL:-http://localhost:8090}"

if command -v curl >/dev/null 2>&1; then
  if ! curl -sS -o /dev/null --max-time 2 "$C4_PROVER_URL"; then
    echo "warning: prover not reachable at $C4_PROVER_URL (continuing anyway)" >&2
  fi
fi

cd "$PKG_DIR"
exec npm run test:rpc-integration -- "$@"
