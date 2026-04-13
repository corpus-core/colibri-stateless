#!/bin/bash
# Build tor-js WASM from source and install it as a local dependency.
#
# Until tor-js is published on npm, this script builds the Arti WASM
# package from source and installs the ts-wrapper into node_modules/
# so that `import('tor-js')` resolves at runtime.  The package.json
# "bundleDependencies" field ensures tor-js is included in the npm
# tarball when publishing.
#
# Prerequisites:
#   - Rust toolchain with wasm32-unknown-unknown target (https://rustup.rs/)
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

# Ensure wasm32 target is installed
rustup target add wasm32-unknown-unknown 2>/dev/null || true

# Clone or update the arti repository
if [ ! -d "$ARTI_DIR" ]; then
    echo "Cloning voltrevo/arti..."
    git clone --depth 1 https://github.com/voltrevo/arti.git "$ARTI_DIR"
else
    echo "Updating existing arti checkout..."
    cd "$ARTI_DIR" && git pull --ff-only
fi

cd "$ARTI_DIR"

# Build WASM + TS wrapper (same steps as the arti showcase CI workflow)
echo "Building WASM with wasm-pack..."
scripts/tor-js/build.sh --release

TS_WRAPPER="$ARTI_DIR/crates/tor-js/ts-wrapper"

echo ""
echo "=== Build complete ==="
echo "WASM package: $ARTI_DIR/crates/tor-js/pkg/"
echo "TS wrapper:   $TS_WRAPPER/dist/"

# Install the local ts-wrapper as 'tor-js' into colibri-thor's node_modules.
# This makes `import('tor-js')` resolve at runtime.
echo ""
echo "Installing tor-js into colibri-thor..."
cd "$THOR_DIR"
npm install "$TS_WRAPPER" --install-links
echo "Done. tor-js is now available via import('tor-js')."
