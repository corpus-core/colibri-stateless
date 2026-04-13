#!/bin/bash
# Build tor-js WASM from source (until tor-js is published on npm).
#
# Prerequisites:
#   - Rust toolchain (https://rustup.rs/)
#   - wasm-pack (https://rustwasm.github.io/wasm-pack/installer/)
#   - Node.js >= 18
#
# Usage:
#   npm run build:arti          # from bindings/emscripten/thor/
#   bash scripts/build-arti.sh  # directly

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
THOR_DIR="$(dirname "$SCRIPT_DIR")"
ARTI_DIR="$THOR_DIR/.arti-build"

echo "=== Building tor-js WASM from source ==="

# Check prerequisites
for cmd in rustc wasm-pack node; do
    if ! command -v "$cmd" &>/dev/null; then
        echo "Error: $cmd not found. Please install it first."
        exit 1
    fi
done

# Clone or update the arti repository
if [ ! -d "$ARTI_DIR" ]; then
    echo "Cloning voltrevo/arti..."
    git clone --depth 1 https://github.com/voltrevo/arti.git "$ARTI_DIR"
else
    echo "Updating existing arti checkout..."
    cd "$ARTI_DIR" && git pull --ff-only
fi

cd "$ARTI_DIR"

# Build WASM (same steps as the showcase CI workflow)
echo "Building WASM with wasm-pack..."
scripts/tor-js/build.sh --release

echo ""
echo "=== Build complete ==="
echo "WASM package: $ARTI_DIR/crates/tor-js/pkg/"
echo "TS wrapper:   $ARTI_DIR/crates/tor-js/ts-wrapper/dist/"
echo ""
echo "To use the vendored build, copy the artifacts into your project"
echo "or install tor-js from the local build:"
echo "  npm install $ARTI_DIR/crates/tor-js/ts-wrapper"
