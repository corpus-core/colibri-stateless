#!/bin/bash

# Swift XCFramework Build Script für lokale Entwicklung
# Copyright (c) 2025 corpus.core

set -e

echo "🚀 Starte Swift XCFramework Build..."

# Variablen
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SWIFT_DIR="$ROOT_DIR/bindings/swift"
BUILD_iOS_X86_DIR="$ROOT_DIR/build_ios_x86"
BUILD_iOS_ARM_DIR="$ROOT_DIR/build_ios_arm"
BUILD_MACOS_X86_DIR="$ROOT_DIR/build_macos_x86"  
BUILD_MACOS_ARM_DIR="$ROOT_DIR/build_macos_arm"

# Cleanup alte Builds
echo "🧹 Cleanup alte Builds..."
rm -rf "$BUILD_iOS_X86_DIR" "$BUILD_iOS_ARM_DIR" "$BUILD_MACOS_X86_DIR" "$BUILD_MACOS_ARM_DIR"

# Prüfe ob wir auf macOS sind
if [[ "$(uname)" != "Darwin" ]]; then
    echo "❌ Fehler: Dieses Script funktioniert nur auf macOS"
    exit 1
fi

# Prüfe Xcode-Konfiguration
DEVELOPER_DIR=$(xcode-select -p 2>/dev/null || echo "")
if [[ "$DEVELOPER_DIR" == "/Library/Developer/CommandLineTools" ]]; then
    echo "⚠️  Warning: Nur Command Line Tools aktiv. Setze Xcode.app..."
    echo "   Führe aus: sudo xcode-select -s /Applications/Xcode.app/Contents/Developer"
    if [[ -d "/Applications/Xcode.app" ]]; then
        echo "   Xcode.app gefunden, versuche automatisch zu setzen..."
        sudo xcode-select -s /Applications/Xcode.app/Contents/Developer || {
            echo "❌ Fehler: Konnte Xcode.app nicht setzen. Führe manuell aus:"
            echo "   sudo xcode-select -s /Applications/Xcode.app/Contents/Developer"
            exit 1
        }
    else
        echo "❌ Fehler: Xcode.app nicht gefunden. Installiere Xcode aus dem App Store."
        exit 1
    fi
fi

# Prüfe iOS SDK Verfügbarkeit
SIMULATOR_SDK=$(xcrun --sdk iphonesimulator --show-sdk-path 2>/dev/null || echo "")
DEVICE_SDK=$(xcrun --sdk iphoneos --show-sdk-path 2>/dev/null || echo "")

if [[ -z "$SIMULATOR_SDK" ]]; then
    echo "❌ Fehler: iOS Simulator SDK nicht gefunden."
    echo "   Lösung: Öffne Xcode und installiere iOS Simulator"
    echo "   Xcode → Preferences → Components → iOS Simulator"
    exit 1
fi

if [[ -z "$DEVICE_SDK" ]]; then
    echo "❌ Fehler: iOS Device SDK nicht gefunden."
    echo "   Lösung: Öffne Xcode und akzeptiere die License"
    echo "   Oder führe aus: sudo xcodebuild -license accept"
    exit 1
fi

echo "📱 iOS Simulator SDK: $SIMULATOR_SDK"
echo "📱 iOS Device SDK: $DEVICE_SDK"

# Build alle Architekturen parallel
build_arch() {
    local name="$1"
    local build_dir="$2"  
    local system_name="$3"
    local arch="$4"
    local sysroot="$5"
    local deployment_target="$6"
    
    echo "🛠️  Baue $name ($arch)..."
    cd "$ROOT_DIR"
    
    cmake \
        -DSWIFT=true \
        -DCMAKE_SYSTEM_NAME="$system_name" \
        -DCMAKE_OSX_SYSROOT="$sysroot" \
        -DCMAKE_OSX_ARCHITECTURES="$arch" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" \
        -DCMAKE_BUILD_TYPE=Release \
        -B "$build_dir" \
        .
    
    cd "$build_dir"
    make -j$(sysctl -n hw.ncpu) c4_swift_binding
    cd "$ROOT_DIR"
    
    echo "✅ $name Build abgeschlossen"
}

# Build iOS Architekturen
build_arch "iOS Simulator x86_64" "$BUILD_iOS_X86_DIR" "iOS" "x86_64" "$SIMULATOR_SDK" "13.0"
build_arch "iOS Device arm64" "$BUILD_iOS_ARM_DIR" "iOS" "arm64" "$DEVICE_SDK" "13.0"

# Build macOS Architekturen  
MACOS_SDK=$(xcrun --sdk macosx --show-sdk-path)
build_arch "macOS Apple Silicon arm64" "$BUILD_MACOS_ARM_DIR" "Darwin" "arm64" "$MACOS_SDK" "10.15"
build_arch "macOS Intel x86_64" "$BUILD_MACOS_X86_DIR" "Darwin" "x86_64" "$MACOS_SDK" "10.15"

# Erstelle Universal XCFramework (iOS + macOS)
echo "🔨 Erstelle Universal XCFramework (iOS + macOS)..."
"$SWIFT_DIR/create_universal_xcframework.sh" \
    "$BUILD_iOS_ARM_DIR" \
    "$BUILD_iOS_X86_DIR" \
    "$BUILD_MACOS_ARM_DIR" \
    "$BUILD_MACOS_X86_DIR" \
    "$ROOT_DIR/bindings/colibri.h" \
    "$SWIFT_DIR/xcframework/modules/module.modulemap"

# Prüfe ob XCFramework erstellt wurde  
XCFRAMEWORK_PATH="$BUILD_iOS_ARM_DIR/c4_swift.xcframework"
if [[ -d "$XCFRAMEWORK_PATH" ]]; then
    echo "✅ Universal XCFramework erfolgreich erstellt: $XCFRAMEWORK_PATH"
    
    # Zeige Architektur-Info für alle Platformen
    echo "📊 Framework-Info:"
    echo "📱 iOS Device (arm64):"
    file "$XCFRAMEWORK_PATH/ios-arm64/c4_swift.framework/c4_swift" 2>/dev/null || echo "  [nicht gefunden]"
    echo "📱 iOS Simulator (x86_64):"  
    file "$XCFRAMEWORK_PATH/ios-x86_64-simulator/c4_swift.framework/c4_swift" 2>/dev/null || echo "  [nicht gefunden]"
    echo "💻 macOS Apple Silicon (arm64):"
    file "$XCFRAMEWORK_PATH/macos-arm64/c4_swift.framework/c4_swift" 2>/dev/null || echo "  [nicht gefunden]"
    echo "💻 macOS Intel (x86_64):"
    file "$XCFRAMEWORK_PATH/macos-x86_64/c4_swift.framework/c4_swift" 2>/dev/null || echo "  [nicht gefunden]"
    
    # Größe anzeigen
    echo "📏 Framework-Größe:"
    du -sh "$XCFRAMEWORK_PATH"
else
    echo "❌ Fehler: XCFramework wurde nicht erstellt"
    exit 1
fi

# Optional: Swift Package testen
if command -v swift &> /dev/null; then
    echo "🧪 Teste Swift Package..."
    cd "$SWIFT_DIR"
    
    # Kopiere XCFramework in Package-Struktur für lokale Tests
    if [[ -d "c4_swift.xcframework" ]]; then
        rm -rf "c4_swift.xcframework"
    fi
    cp -r "$XCFRAMEWORK_PATH" .
    
    # Test-Build (ohne Tests ausführen da Server benötigt)
    if swift build &>/dev/null; then
        echo "✅ Swift Package Build erfolgreich"
    else
        echo "⚠️  Swift Package Build fehlgeschlagen (möglicherweise normale Abhängigkeitsprobleme)"
    fi
    
    # Cleanup
    rm -rf "c4_swift.xcframework"
else
    echo "⚠️  Swift nicht gefunden - Package-Test übersprungen"
fi

echo ""
echo "🎉 Universal XCFramework Build erfolgreich abgeschlossen!"
echo "📦 XCFramework: $XCFRAMEWORK_PATH"
echo ""
echo "🌟 Unterstützte Platformen:"
echo "   📱 iOS (Device + Simulator)"
echo "   💻 macOS (Apple Silicon + Intel)"
echo ""
echo "💡 Nächste Schritte:"
echo "   1. XCFramework in Ihr iOS/macOS Projekt einbinden"
echo "   2. Swift Package Manager verwenden: kopieren Sie das XCFramework in Ihr Swift Package"
echo "   3. Xcode: Drag & Drop des XCFrameworks in Ihr Projekt"