#!/bin/bash

# iOS XCFramework Build Script
# Copyright (c) 2025 corpus.core

set -e

echo "📱 Starte iOS XCFramework Build..."

# Variablen
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SWIFT_DIR="$ROOT_DIR/bindings/swift"
BUILD_iOS_X86_DIR="$ROOT_DIR/build_ios_x86"
BUILD_iOS_ARM_DIR="$ROOT_DIR/build_ios_arm"

# Cleanup alte iOS Builds
echo "🧹 Cleanup alte iOS Builds..."
rm -rf "$BUILD_iOS_X86_DIR" "$BUILD_iOS_ARM_DIR"

# Prüfe ob wir auf macOS sind
if [[ "$(uname)" != "Darwin" ]]; then
    echo "❌ Fehler: iOS Build funktioniert nur auf macOS"
    exit 1
fi

# Prüfe iOS SDK Verfügbarkeit
SIMULATOR_SDK=$(xcrun --sdk iphonesimulator --show-sdk-path 2>/dev/null || echo "")
DEVICE_SDK=$(xcrun --sdk iphoneos --show-sdk-path 2>/dev/null || echo "")

if [[ -z "$SIMULATOR_SDK" ]]; then
    echo "❌ Fehler: iOS Simulator SDK nicht gefunden."
    echo "   Lösung: Installiere Xcode und iOS Simulator Support"
    exit 1
fi

if [[ -z "$DEVICE_SDK" ]]; then
    echo "❌ Fehler: iOS Device SDK nicht gefunden."
    echo "   Lösung: Installiere Xcode und akzeptiere die License"
    exit 1
fi

echo "📱 iOS Simulator SDK: $SIMULATOR_SDK"
echo "📱 iOS Device SDK: $DEVICE_SDK"

# Build Funktion
build_ios_arch() {
    local name="$1"
    local build_dir="$2"  
    local arch="$3"
    local sysroot="$4"
    
    echo "🛠️  Baue $name ($arch)..."
    cd "$ROOT_DIR"
    
    cmake \
        -DSWIFT=true \
        -DCMAKE_SYSTEM_NAME="iOS" \
        -DCMAKE_OSX_SYSROOT="$sysroot" \
        -DCMAKE_OSX_ARCHITECTURES="$arch" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="13.0" \
        -DCMAKE_BUILD_TYPE=Release \
        -B "$build_dir" \
        .
    
    cd "$build_dir"
    make -j$(sysctl -n hw.ncpu) c4_swift_binding
    cd "$ROOT_DIR"
    
    echo "✅ $name Build abgeschlossen"
}

# Build iOS Architekturen
build_ios_arch "iOS Simulator x86_64" "$BUILD_iOS_X86_DIR" "x86_64" "$SIMULATOR_SDK"
build_ios_arch "iOS Device arm64" "$BUILD_iOS_ARM_DIR" "arm64" "$DEVICE_SDK"

# Erstelle iOS XCFramework
echo "🔨 Erstelle iOS XCFramework (Device + Simulator)..."
"$SWIFT_DIR/create_ios_xcframework.sh" \
    "$BUILD_iOS_ARM_DIR" \
    "$BUILD_iOS_X86_DIR" \
    "$ROOT_DIR/bindings/colibri.h" \
    "$SWIFT_DIR/xcframework/modules/module.modulemap"

# Prüfe Ergebnis
XCFRAMEWORK_PATH="$BUILD_iOS_ARM_DIR/c4_swift.xcframework"
if [[ -d "$XCFRAMEWORK_PATH" ]]; then
    echo "✅ iOS XCFramework erfolgreich erstellt: $XCFRAMEWORK_PATH"
    
    # Zeige Architektur-Info
    echo "📊 Framework-Info:"
    echo "📱 iOS Device (arm64):"
    file "$XCFRAMEWORK_PATH/ios-arm64/c4_swift.framework/c4_swift" 2>/dev/null || echo "  [nicht gefunden]"
    echo "📱 iOS Simulator (x86_64):"  
    file "$XCFRAMEWORK_PATH/ios-x86_64-simulator/c4_swift.framework/c4_swift" 2>/dev/null || echo "  [nicht gefunden]"
    
    # Größe anzeigen
    echo "📏 Framework-Größe:"
    du -sh "$XCFRAMEWORK_PATH"
    
    echo ""
    echo "🎉 iOS XCFramework Build erfolgreich!"
    echo "📦 Pfad: $XCFRAMEWORK_PATH"
    echo "💡 Verwendung: Drag & Drop in iOS Xcode Projekt"
else
    echo "❌ Fehler: iOS XCFramework wurde nicht erstellt"
    exit 1
fi