#!/bin/bash
set -e

# Build Colibri Rust bindings using CMake
# Supports cross-compilation via --target flag

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Default values
TARGET=""
BUILD_TYPE="Release"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --target)
            TARGET="$2"
            shift 2
            ;;
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --help)
            echo "Usage: ./build.sh [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --target <triple>  Cross-compile for target (e.g., aarch64-apple-darwin)"
            echo "  --debug            Build in debug mode"
            echo "  --help             Show this help"
            echo ""
            echo "Common targets:"
            echo "  x86_64-apple-darwin       macOS Intel"
            echo "  aarch64-apple-darwin      macOS Apple Silicon"
            echo "  x86_64-unknown-linux-gnu  Linux x86_64"
            echo "  aarch64-unknown-linux-gnu Linux ARM64"
            echo "  aarch64-apple-ios         iOS"
            echo "  aarch64-linux-android     Android ARM64"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Determine host target if not specified
if [ -z "$TARGET" ]; then
    # Detect host target
    ARCH=$(uname -m)
    OS=$(uname -s)

    if [[ "$OS" == "Darwin" ]]; then
        if [[ "$ARCH" == "arm64" ]]; then
            TARGET="aarch64-apple-darwin"
        else
            TARGET="x86_64-apple-darwin"
        fi
    elif [[ "$OS" == "Linux" ]]; then
        if [[ "$ARCH" == "aarch64" ]]; then
            TARGET="aarch64-unknown-linux-gnu"
        else
            TARGET="x86_64-unknown-linux-gnu"
        fi
    else
        echo "Unsupported OS: $OS"
        exit 1
    fi
    echo "Detected host target: $TARGET"
fi

# Set up build directory (target-specific)
BUILD_DIR="$PROJECT_ROOT/build/$TARGET"
RUST_TARGET_DIR="$SCRIPT_DIR/target/colibri/$TARGET"

echo "Building Colibri C library for target: $TARGET"
echo "Build directory: $BUILD_DIR"

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Set up CMake options based on target
CMAKE_OPTS="-DCMAKE_BUILD_TYPE=$BUILD_TYPE"

# Cross-compilation setup
case $TARGET in
    aarch64-apple-darwin)
        if [[ "$(uname -s)" == "Darwin" ]]; then
            CMAKE_OPTS="$CMAKE_OPTS -DCMAKE_OSX_ARCHITECTURES=arm64"
        else
            echo "Cross-compiling to macOS ARM64 from non-macOS is not supported"
            exit 1
        fi
        ;;
    x86_64-apple-darwin)
        if [[ "$(uname -s)" == "Darwin" ]]; then
            CMAKE_OPTS="$CMAKE_OPTS -DCMAKE_OSX_ARCHITECTURES=x86_64"
        else
            echo "Cross-compiling to macOS x86_64 from non-macOS is not supported"
            exit 1
        fi
        ;;
    aarch64-unknown-linux-gnu)
        if [[ "$(uname -m)" != "aarch64" ]]; then
            # Cross-compiling to ARM64 Linux
            CMAKE_OPTS="$CMAKE_OPTS -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc"
            CMAKE_OPTS="$CMAKE_OPTS -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++"
            CMAKE_OPTS="$CMAKE_OPTS -DCMAKE_SYSTEM_NAME=Linux"
            CMAKE_OPTS="$CMAKE_OPTS -DCMAKE_SYSTEM_PROCESSOR=aarch64"
        fi
        ;;
    x86_64-unknown-linux-gnu)
        if [[ "$(uname -m)" == "aarch64" ]]; then
            # Cross-compiling to x86_64 Linux from ARM64
            CMAKE_OPTS="$CMAKE_OPTS -DCMAKE_C_COMPILER=x86_64-linux-gnu-gcc"
            CMAKE_OPTS="$CMAKE_OPTS -DCMAKE_CXX_COMPILER=x86_64-linux-gnu-g++"
            CMAKE_OPTS="$CMAKE_OPTS -DCMAKE_SYSTEM_NAME=Linux"
            CMAKE_OPTS="$CMAKE_OPTS -DCMAKE_SYSTEM_PROCESSOR=x86_64"
        fi
        ;;
    aarch64-apple-ios)
        if [[ "$(uname -s)" == "Darwin" ]]; then
            CMAKE_OPTS="$CMAKE_OPTS -DCMAKE_SYSTEM_NAME=iOS"
            CMAKE_OPTS="$CMAKE_OPTS -DCMAKE_OSX_ARCHITECTURES=arm64"
            CMAKE_OPTS="$CMAKE_OPTS -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0"
        else
            echo "Cross-compiling to iOS from non-macOS is not supported"
            exit 1
        fi
        ;;
    aarch64-linux-android)
        if [ -z "$ANDROID_NDK_HOME" ]; then
            echo "ANDROID_NDK_HOME not set. Please set it to your Android NDK path."
            exit 1
        fi
        CMAKE_OPTS="$CMAKE_OPTS -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"
        CMAKE_OPTS="$CMAKE_OPTS -DANDROID_ABI=arm64-v8a"
        CMAKE_OPTS="$CMAKE_OPTS -DANDROID_PLATFORM=android-21"
        ;;
    *)
        echo "Warning: No specific cross-compilation setup for $TARGET, using defaults"
        ;;
esac

# Configure with CMake
echo "Configuring CMake with options: $CMAKE_OPTS"
cmake "$PROJECT_ROOT" $CMAKE_OPTS

# Build the C libraries
echo "Building C libraries..."
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Create combined static library
echo "Creating combined static library for Rust..."
mkdir -p "$RUST_TARGET_DIR"

# List of libraries to combine
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

if [[ "$(uname -s)" == "Darwin" ]]; then
    # macOS - use libtool to create a combined library
    echo "Combining libraries for macOS..."
    libtool -static -o "$RUST_TARGET_DIR/libcolibri_combined.a" "${LIBS[@]}"
else
    # Linux - use ar to combine
    echo "Combining libraries for Linux..."

    # Create a temporary directory for extraction
    TEMP_DIR=$(mktemp -d)
    cd "$TEMP_DIR"

    # Extract all object files
    for lib in "${LIBS[@]}"; do
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

# Build Rust bindings if not cross-compiling or if target matches host
HOST_TARGET=""
ARCH=$(uname -m)
OS=$(uname -s)
if [[ "$OS" == "Darwin" ]]; then
    if [[ "$ARCH" == "arm64" ]]; then
        HOST_TARGET="aarch64-apple-darwin"
    else
        HOST_TARGET="x86_64-apple-darwin"
    fi
elif [[ "$OS" == "Linux" ]]; then
    if [[ "$ARCH" == "aarch64" ]]; then
        HOST_TARGET="aarch64-unknown-linux-gnu"
    else
        HOST_TARGET="x86_64-unknown-linux-gnu"
    fi
fi

if [ "$TARGET" == "$HOST_TARGET" ]; then
    echo "Building Rust bindings..."
    cd "$SCRIPT_DIR"
    cargo build
    echo "Build complete!"
else
    echo ""
    echo "Cross-compilation complete!"
    echo "C library built for: $TARGET"
    echo ""
    echo "To build Rust bindings, run:"
    echo "  cargo build --target $TARGET"
fi
