#!/bin/bash
set -e

# Build Colibri Rust bindings using CMake
# This approach is similar to Python bindings - let CMake handle dependencies

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

echo "Building Colibri C library with CMake..."

# Create build directory if it doesn't exist
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake if not already configured
if [ ! -f "Makefile" ]; then
    echo "Configuring CMake..."
    cmake .. -DCMAKE_BUILD_TYPE=Release
fi

# Build the C libraries
echo "Building C libraries..."
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Create a single static library combining all dependencies (platform-specific)
echo "Creating combined static library for Rust..."
RUST_TARGET_DIR="$SCRIPT_DIR/target/colibri"
mkdir -p "$RUST_TARGET_DIR"

if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS - use libtool to create a combined library
    echo "Combining libraries for macOS..."

    # Collect all .a files
    LIBS=(
        "$BUILD_DIR/src/prover/libprover.a"
        "$BUILD_DIR/src/verifier/libverifier.a"
        "$BUILD_DIR/src/chains/eth/libeth_prover.a"
        "$BUILD_DIR/src/chains/eth/libeth_verifier.a"
        "$BUILD_DIR/src/chains/eth/zk_verifier/libzk_verifier.a"
        "$BUILD_DIR/src/chains/eth/bn254/libeth_bn254.a"
        "$BUILD_DIR/src/chains/eth/precompiles/libeth_precompiles.a"
        "$BUILD_DIR/src/util/libutil.a"
        "$BUILD_DIR/libs/crypto/libcrypto.a"
        "$BUILD_DIR/_deps/ethhash_external-build/libkeccak.a"
        "$BUILD_DIR/libs/blst/libblst.a"
        "$BUILD_DIR/libs/curl/libcurl_fetch.a"
        "$BUILD_DIR/libs/evmone/libevmone_wrapper.a"
        "$BUILD_DIR/_deps/evmone_external-build/libevmone.a"
        "$BUILD_DIR/libs/intx/libintx_wrapper.a"
    )

    # Create combined library
    libtool -static -o "$RUST_TARGET_DIR/libcolibri_combined.a" "${LIBS[@]}"

else
    # Linux - use ar to combine
    echo "Combining libraries for Linux..."

    # Create a temporary directory for extraction
    TEMP_DIR=$(mktemp -d)
    cd "$TEMP_DIR"

    # Extract all object files
    for lib in \
        "$BUILD_DIR/src/prover/libprover.a" \
        "$BUILD_DIR/src/verifier/libverifier.a" \
        "$BUILD_DIR/src/chains/eth/libeth_prover.a" \
        "$BUILD_DIR/src/chains/eth/libeth_verifier.a" \
        "$BUILD_DIR/src/chains/eth/zk_verifier/libzk_verifier.a" \
        "$BUILD_DIR/src/chains/eth/bn254/libeth_bn254.a" \
        "$BUILD_DIR/src/chains/eth/precompiles/libeth_precompiles.a" \
        "$BUILD_DIR/src/util/libutil.a" \
        "$BUILD_DIR/libs/crypto/libcrypto.a" \
        "$BUILD_DIR/_deps/ethhash_external-build/libkeccak.a" \
        "$BUILD_DIR/libs/blst/libblst.a" \
        "$BUILD_DIR/libs/curl/libcurl_fetch.a" \
        "$BUILD_DIR/libs/evmone/libevmone_wrapper.a" \
        "$BUILD_DIR/_deps/evmone_external-build/libevmone.a" \
        "$BUILD_DIR/libs/intx/libintx_wrapper.a"
    do
        if [ -f "$lib" ]; then
            ar x "$lib"
        fi
    done

    # Create combined library
    ar rcs "$RUST_TARGET_DIR/libcolibri_combined.a" *.o

    # Clean up
    cd -
    rm -rf "$TEMP_DIR"
fi

echo "Combined library created at: $RUST_TARGET_DIR/libcolibri_combined.a"

# Now build the Rust bindings
echo "Building Rust bindings..."
cd "$SCRIPT_DIR"
cargo build

echo "Build complete!"