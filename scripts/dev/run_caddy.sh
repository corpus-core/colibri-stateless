#!/usr/bin/env bash
#
# Start the local Caddy dev instance via docker compose:
#   - static file server on :8080 (serves the repo, replaces python http.server)
#   - CORS-enabled reverse proxy on :8091 -> local prover on :8090
# Then open the browser debug page.
#
# Usage:
#   scripts/dev/run_caddy.sh
#
# Env overrides:
#   CADDY_IMAGE   docker image to use (default: local caddy-lb:latest if present,
#                 otherwise caddy:2.10.0)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPOSE_DIR="$SCRIPT_DIR/lb"
DEBUG_URL="http://localhost:8080/bindings/emscripten/test/debug.html"

# --- pick the docker compose command (v2 plugin vs legacy binary) ------------
if docker compose version >/dev/null 2>&1; then
  COMPOSE=(docker compose)
elif command -v docker-compose >/dev/null 2>&1; then
  COMPOSE=(docker-compose)
else
  echo "error: neither 'docker compose' nor 'docker-compose' is available" >&2
  exit 1
fi

# --- prefer a locally present image to avoid slow/failing registry pulls -----
if [[ -z "${CADDY_IMAGE:-}" ]]; then
  if docker image inspect caddy-lb:latest >/dev/null 2>&1; then
    export CADDY_IMAGE=caddy-lb:latest
  else
    export CADDY_IMAGE=caddy:2.10.0
  fi
fi

# --- warn if :8080 is still taken (e.g. a leftover python http.server) -------
if command -v lsof >/dev/null 2>&1 && lsof -iTCP:8080 -sTCP:LISTEN >/dev/null 2>&1; then
  echo "warning: something is already listening on :8080." >&2
  echo "         Stop it first (e.g. the 'python3 -m http.server 8080')." >&2
fi

open_url() {
  if command -v open >/dev/null 2>&1; then
    open "$1"
  elif command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$1"
  else
    echo "Open this URL manually: $1"
  fi
}

cleanup() {
  echo
  echo "Stopping caddy..."
  ( cd "$COMPOSE_DIR" && "${COMPOSE[@]}" down )
}
trap cleanup INT TERM

cd "$COMPOSE_DIR"

echo "Using CADDY_IMAGE=$CADDY_IMAGE"
"${COMPOSE[@]}" up -d

echo
echo "Caddy is up:"
echo "  static -> http://localhost:8080  (serving the repo root)"
echo "  prover -> http://localhost:8091  (CORS proxy to :8090)"
echo
echo "Opening $DEBUG_URL"
open_url "$DEBUG_URL"

echo
echo "Following logs -- press Ctrl-C to stop and shut the container down."
"${COMPOSE[@]}" logs -f
