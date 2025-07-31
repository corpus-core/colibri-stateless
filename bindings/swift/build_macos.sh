#!/bin/bash

# macOS Static Libraries Build Script
# Copyright (c) 2025 corpus.core

set -e

# Parse command line arguments
DEV_MODE=false
for arg in "$@"; do
    case $arg in
        -dev|--dev)
            DEV_MODE=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  -dev, --dev    Development mode (current arch only, incremental builds)"
            echo "  -h, --help     Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Use -h or --help for usage information"
            exit 1
            ;;
    esac
done

if [[ "$DEV_MODE" == "true" ]]; then
    echo "💻 Starte macOS Development Build (incremental, current arch only)..."
else
    echo "💻 Starte macOS Static Libraries Build (full, both architectures)..."
fi

# Variablen
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SWIFT_DIR="$ROOT_DIR/bindings/swift"
BUILD_MACOS_ARM_DIR="$ROOT_DIR/build_macos_arm"
BUILD_MACOS_X86_DIR="$ROOT_DIR/build_macos_x86"

# Detect current architecture
CURRENT_ARCH=$(uname -m)
if [[ "$CURRENT_ARCH" == "arm64" ]]; then
    DEV_BUILD_DIR="$BUILD_MACOS_ARM_DIR"
    DEV_ARCH="arm64"
    DEV_ARCH_NAME="Apple Silicon"
else
    DEV_BUILD_DIR="$BUILD_MACOS_X86_DIR"
    DEV_ARCH="x86_64"
    DEV_ARCH_NAME="Intel"
fi

# Cleanup logic
if [[ "$DEV_MODE" == "true" ]]; then
    echo "🔧 Development Mode aktiv"
    echo "   Current Architecture: $DEV_ARCH_NAME ($DEV_ARCH)"
    echo "   Build Directory: $DEV_BUILD_DIR"
    
    # In dev mode, only clean if explicitly requested or if build seems broken
    if [[ ! -f "$DEV_BUILD_DIR/CMakeCache.txt" ]]; then
        echo "🧹 CMakeCache nicht gefunden, cleanup Build-Verzeichnis..."
        rm -rf "$DEV_BUILD_DIR"
    else
        echo "♻️  Inkrementeller Build (verwende bestehendes Build-Verzeichnis)"
    fi
else
    echo "🧹 Cleanup alte macOS Builds..."
    rm -rf "$BUILD_MACOS_ARM_DIR" "$BUILD_MACOS_X86_DIR"
fi

# Prüfe ob wir auf macOS sind
if [[ "$(uname)" != "Darwin" ]]; then
    echo "❌ Fehler: macOS Build funktioniert nur auf macOS"
    exit 1
fi

# Prüfe macOS SDK
MACOS_SDK=$(xcrun --sdk macosx --show-sdk-path 2>/dev/null || echo "")
if [[ -z "$MACOS_SDK" ]]; then
    echo "❌ Fehler: macOS SDK nicht gefunden."
    echo "   Lösung: Installiere Xcode Command Line Tools"
    exit 1
fi

echo "💻 macOS SDK: $MACOS_SDK"

# Build Funktion
build_macos_arch() {
    local name="$1"
    local build_dir="$2"  
    local arch="$3"
    local incremental="$4"
    
    echo "🛠️  Baue $name ($arch)..."
    cd "$ROOT_DIR"
    
    if [[ "$incremental" == "true" ]] && [[ -f "$build_dir/CMakeCache.txt" ]]; then
        echo "♻️  Inkrementeller Build: überspringe CMake-Konfiguration"
    else
        echo "🔧 Konfiguriere CMake..."
        cmake \
            -DSWIFT=true \
            -DCMAKE_SYSTEM_NAME="Darwin" \
            -DCMAKE_OSX_SYSROOT="$MACOS_SDK" \
            -DCMAKE_OSX_ARCHITECTURES="$arch" \
            -DCMAKE_OSX_DEPLOYMENT_TARGET="10.15" \
            -DCMAKE_BUILD_TYPE=Release \
            -B "$build_dir" \
            .
    fi
    
    cd "$build_dir"
    echo "🔨 Baue Libraries..."
    make -j$(sysctl -n hw.ncpu) c4_swift_binding
    cd "$ROOT_DIR"
    
    echo "✅ $name Build abgeschlossen"
}

# Build macOS Architekturen
if [[ "$DEV_MODE" == "true" ]]; then
    # Development mode: nur aktuelle Architektur
    build_macos_arch "macOS $DEV_ARCH_NAME $DEV_ARCH" "$DEV_BUILD_DIR" "$DEV_ARCH" "true"
else
    # Production mode: beide Architekturen
    build_macos_arch "macOS Apple Silicon arm64" "$BUILD_MACOS_ARM_DIR" "arm64" "false"
    build_macos_arch "macOS Intel x86_64" "$BUILD_MACOS_X86_DIR" "x86_64" "false"
fi

# Prüfe Ergebnisse
echo "📊 Verfügbare macOS Libraries:"

if [[ "$DEV_MODE" == "true" ]]; then
    # Dev mode: prüfe nur die aktuelle Architektur
    if [[ -f "$DEV_BUILD_DIR/bindings/swift/libc4_swift_binding.a" ]]; then
        echo "✅ macOS $DEV_ARCH_NAME ($DEV_ARCH) Libraries:"
        echo "   📁 $DEV_BUILD_DIR"
        echo "   📋 $(find "$DEV_BUILD_DIR" -name "*.a" | wc -l | xargs) static libraries"
        echo "   📏 $(du -sh "$DEV_BUILD_DIR" | cut -f1) total size"
    else
        echo "❌ macOS $DEV_ARCH_NAME Libraries nicht gefunden"
        exit 1
    fi
else
    # Production mode: prüfe beide Architekturen
    # Prüfe arm64 Libraries
    if [[ -f "$BUILD_MACOS_ARM_DIR/bindings/swift/libc4_swift_binding.a" ]]; then
        echo "✅ macOS arm64 Libraries:"
        echo "   📁 $BUILD_MACOS_ARM_DIR"
        echo "   📋 $(find "$BUILD_MACOS_ARM_DIR" -name "*.a" | wc -l | xargs) static libraries"
        echo "   📏 $(du -sh "$BUILD_MACOS_ARM_DIR" | cut -f1) total size"
    else
        echo "❌ macOS arm64 Libraries nicht gefunden"
        exit 1
    fi

    # Prüfe x86_64 Libraries  
    if [[ -f "$BUILD_MACOS_X86_DIR/bindings/swift/libc4_swift_binding.a" ]]; then
        echo "✅ macOS x86_64 Libraries:"
        echo "   📁 $BUILD_MACOS_X86_DIR"
        echo "   📋 $(find "$BUILD_MACOS_X86_DIR" -name "*.a" | wc -l | xargs) static libraries"
        echo "   📏 $(du -sh "$BUILD_MACOS_X86_DIR" | cut -f1) total size"
    else
        echo "❌ macOS x86_64 Libraries nicht gefunden"
        exit 1
    fi
fi

echo ""
if [[ "$DEV_MODE" == "true" ]]; then
    echo "🎉 macOS Development Build erfolgreich!"
    echo ""
    echo "⚡ Development Mode Vorteile:"
    echo "   ♻️  Inkrementelle Builds (schneller bei Änderungen)"
    echo "   🎯 Nur aktuelle Architektur ($DEV_ARCH_NAME)"
    echo "   💾 Spart Speicherplatz und Build-Zeit"
    echo ""
    echo "💡 Nächste Schritte:"
    echo "   cd bindings/swift"
    echo "   swift test  # Nutzt die gebauten Libraries"
    echo ""
    echo "💡 Für Production Build:"
    echo "   ./build_macos.sh  # Ohne -dev für beide Architekturen"
else
    echo "🎉 macOS Static Libraries Build erfolgreich!"
    echo ""
    echo "💡 Verwendung für SPM Tests:"
    echo "   cd bindings/swift"
    echo "   swift test  # Nutzt Package.swift mit relativen Pfaden"
    echo ""
    echo "💡 Verwendung für macOS Entwicklung:"
    echo "   📁 Apple Silicon: $BUILD_MACOS_ARM_DIR"
    echo "   📁 Intel: $BUILD_MACOS_X86_DIR"
    echo ""
    echo "💡 Für schnellere Development Builds:"
    echo "   ./build_macos.sh -dev  # Nur aktuelle Architektur, inkrementell"
fi

# Generate TestConfig.swift for integration tests (embedded in source code)
echo "📝 Generiere TestConfig.swift für Integration Tests..."
TEST_CONFIG_FILE="$SWIFT_DIR/Sources/TestConfig/TestConfig.swift"
TEST_DATA_PATH="$ROOT_DIR/test/data"

# Create TestConfig directory if it doesn't exist
mkdir -p "$SWIFT_DIR/Sources/TestConfig"

cat > "$TEST_CONFIG_FILE" << EOF
// Generated by build_macos.sh. Do not edit manually.
// This file contains build-time configuration for integration tests.

import Foundation

public struct TestConfig {
    /// Path to test data directory (configured at build time)
    public static let testDataPath = "$TEST_DATA_PATH"
    
    /// Get test data URL
    public static var testDataURL: URL {
        return URL(fileURLWithPath: testDataPath)
    }
}
EOF

echo "✅ TestConfig.swift erstellt: $TEST_CONFIG_FILE"