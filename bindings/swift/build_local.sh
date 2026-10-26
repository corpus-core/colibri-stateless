#!/bin/bash

# Swift Complete Build Script für lokale Entwicklung
# Copyright (c) 2025,2026 corpus.core GmbH

set -e

# Parse command line arguments
DEV_MODE=""
for arg in "$@"; do
    case $arg in
        -dev|--dev)
            DEV_MODE="-dev"
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  -dev, --dev    Development mode (macOS: current arch only, incremental builds)"
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

if [[ "$DEV_MODE" == "-dev" ]]; then
    echo "🚀 Starte Development Build (iOS + macOS dev mode)..."
else
    echo "🚀 Starte Complete Swift Build (iOS + macOS)..."
fi

# Variablen
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SWIFT_DIR="$ROOT_DIR/bindings/swift"

# Cleanup Logik
if [[ "$DEV_MODE" == "-dev" ]]; then
    echo "🔧 Development Mode: Cleanup nur iOS builds, macOS build ist inkrementell"
    rm -rf "$ROOT_DIR"/build_ios_*
else
    echo "🧹 Cleanup alle Builds..."
    rm -rf "$ROOT_DIR"/build_ios_* "$ROOT_DIR"/build_macos_*
fi

# Führe iOS Build aus
echo "📱 Starte iOS Build..."
"$SWIFT_DIR/build_ios.sh"

# Führe macOS Build aus
echo ""
echo "💻 Starte macOS Build..."
"$SWIFT_DIR/build_macos.sh" $DEV_MODE

echo ""
if [[ "$DEV_MODE" == "-dev" ]]; then
    echo "🎉 Development Build erfolgreich abgeschlossen!"
    echo ""
    echo "🌟 Ergebnisse:"
    echo "   📱 iOS XCFramework: build_ios_arm/c4_swift.xcframework"
    echo "   💻 macOS Libraries: build_macos_arm/ (nur aktuelle Architektur)"
    echo ""
    echo "⚡ Development Mode Vorteile:"
    echo "   📱 iOS XCFramework: Vollständiger Build (für Distribution)"
    echo "   💻 macOS Libraries: Inkrementeller Build (schneller für Tests)"
    echo ""
    echo "💡 Verwendung:"
    echo "   🧪 swift test  # Nutzt macOS libraries für Tests"
    echo "   📱 iOS App: Nutzt XCFramework für Distribution"
else
    echo "🎉 Complete Swift Build erfolgreich abgeschlossen!"
    echo ""
    echo "🌟 Ergebnisse:"
    echo "   📱 iOS XCFramework: build_ios_arm/c4_swift.xcframework"
    echo "   💻 macOS Libraries: build_macos_arm/ & build_macos_x86/"
    echo ""
    echo "💡 Verwendung:"
    echo "   📱 iOS App Integration: Drag & Drop des XCFrameworks"
    echo "   💻 macOS Development: SPM mit lokalen static libraries"
    echo "   🧪 CI Testing: Nutzt macOS libraries für swift test"
    echo ""
    echo "💡 Für schnellere Development Builds:"
    echo "   ./build_local.sh -dev  # iOS + macOS (inkrementell)"
fi