#!/bin/bash
# build.sh - Build-Skript für die Kona-P2P Bridge

set -e  # Exit bei Fehlern

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build/default"

echo "🔨 Building Kona-P2P OP Stack Bridge"
echo "📁 Script dir: $SCRIPT_DIR"
echo "📁 Project root: $PROJECT_ROOT"
echo "📁 Build dir: $BUILD_DIR"

# Stelle sicher, dass Rust installiert ist
if ! command -v rustc &> /dev/null; then
    echo "❌ Rust ist nicht installiert. Installiere Rust mit:"
    echo "   curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh"
    exit 1
fi

# Stelle sicher, dass Build-Directory existiert
mkdir -p "$BUILD_DIR/lib"
mkdir -p "$BUILD_DIR/include"

# Wechsle ins Kona-Bridge-Verzeichnis
cd "$SCRIPT_DIR"

echo "🦀 Compiling Rust library..."

# Build für C-Integration (cdylib)
cargo build --release

# Build für Standalone-Binary
cargo build --release --bin kona_bridge

echo "📦 Copying build artifacts..."

# Copy shared library
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    cp target/release/libkona_bridge.dylib "$BUILD_DIR/lib/"
    echo "✅ Copied libkona_bridge.dylib"
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    # Linux
    cp target/release/libkona_bridge.so "$BUILD_DIR/lib/"
    echo "✅ Copied libkona_bridge.so"
else
    # Windows (falls unterstützt)
    cp target/release/kona_bridge.dll "$BUILD_DIR/lib/" 2>/dev/null || true
    echo "✅ Copied kona_bridge.dll (if exists)"
fi

# Copy binary
cp target/release/kona_bridge "$BUILD_DIR/bin/" 2>/dev/null || true
echo "✅ Copied kona_bridge binary"

# Copy header
cp kona_bridge.h "$BUILD_DIR/include/"
echo "✅ Copied kona_bridge.h"

echo "🎉 Kona-P2P Bridge build completed!"
echo ""
echo "📋 Usage:"
echo "   Standalone: $BUILD_DIR/bin/kona_bridge --chain-id 8453 --output-dir ./preconfs"
echo "   C Library:  #include <kona_bridge.h> and link with -lkona_bridge"
echo ""
echo "📚 Integration examples:"
echo "   See: $SCRIPT_DIR/examples/"
