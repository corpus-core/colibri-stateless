#!/bin/bash

# iOS XCFramework Build Script
# Copyright (c) 2025 corpus.core

set -e

echo "📱 Starte iOS XCFramework Build..."

# Variablen
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SWIFT_DIR="$ROOT_DIR/bindings/swift"
BUILD_ROOT="$ROOT_DIR/build/ios"
BUILD_iOS_X86_DIR="$BUILD_ROOT/ios_x86_64"
BUILD_iOS_ARM_DIR="$BUILD_ROOT/ios_arm64"

# Cleanup alte iOS Builds
echo "🧹 Cleanup alte iOS Builds..."
rm -rf "$BUILD_ROOT"

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
        -DCHAIN_OP=ON \
        -DETH_ZKPROOF=true \
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
echo "🔨 Erstelle iOS XCFramework (Device + Simulator x86_64)..."
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
else
    echo "❌ Fehler: iOS XCFramework wurde nicht erstellt"
    exit 1
fi

# Erstelle iOS Distribution Package
echo ""
echo "📦 Erstelle iOS Distribution Package..."
iOS_PACKAGE_DIR="$SWIFT_DIR/ios_package"
rm -rf "$iOS_PACKAGE_DIR"
mkdir -p "$iOS_PACKAGE_DIR"

# Kopiere Swift Package Dateien
echo "📋 Kopiere Swift Sources und Tests..."
cp -r "$SWIFT_DIR/Sources" "$iOS_PACKAGE_DIR/"
cp -r "$SWIFT_DIR/Tests" "$iOS_PACKAGE_DIR/"

# Kopiere Header-Dateien direkt ins iOS Package, um relative Pfade zu vermeiden
echo "📋 Kopiere alle benötigten C-Headers ins iOS Package..."
mkdir -p "$iOS_PACKAGE_DIR/Sources/CColibri/include"

# Kopiere alle util headers (mit Abhängigkeiten)
echo "📋 Kopiere src/util/*.h Headers..."
cp "$ROOT_DIR/src/util"/*.h "$iOS_PACKAGE_DIR/Sources/CColibri/include/"

# Kopiere bindings/colibri.h (Haupt-API Header)
echo "📋 Kopiere bindings/colibri.h..."
cp "$ROOT_DIR/bindings/colibri.h" "$iOS_PACKAGE_DIR/Sources/CColibri/include/"

# Passe swift_storage_bridge.c an, um lokale Header zu verwenden
echo "📋 Passe swift_storage_bridge.c für iOS Package an..."
sed -i '' 's|#include "../../../../src/util/bytes.h"|#include "bytes.h"|g' "$iOS_PACKAGE_DIR/Sources/CColibri/swift_storage_bridge.c"
sed -i '' 's|#include "../../../../src/util/plugin.h"|#include "plugin.h"|g' "$iOS_PACKAGE_DIR/Sources/CColibri/swift_storage_bridge.c"

# Kopiere Dokumentation
echo "📋 Kopiere Dokumentation..."
if [[ -f "$SWIFT_DIR/README.md" ]]; then
    cp "$SWIFT_DIR/README.md" "$iOS_PACKAGE_DIR/README.md"
else
    echo "⚠️ README.md nicht gefunden, wird übersprungen"
fi

if [[ -f "$SWIFT_DIR/doc.md" ]]; then
    cp "$SWIFT_DIR/doc.md" "$iOS_PACKAGE_DIR/doc.md"
else
    echo "⚠️ doc.md nicht gefunden, wird übersprungen"
fi

# Kopiere iOS XCFramework
echo "📋 Kopiere iOS XCFramework..."
cp -r "$XCFRAMEWORK_PATH" "$iOS_PACKAGE_DIR/"

# Erstelle Distribution Package.swift für iOS
echo "📋 Erstelle Distribution Package.swift..."
cat > "$iOS_PACKAGE_DIR/Package.swift" << 'EOF'
// swift-tools-version:5.3
import PackageDescription

let package = Package(
    name: "Colibri",
    platforms: [.iOS(.v13)],
    products: [
        .library(name: "Colibri", targets: ["Colibri"])
    ],
    targets: [
        .binaryTarget(
            name: "c4_swift",
            path: "c4_swift.xcframework"
        ),
        .target(
            name: "CColibriMacOS",
            dependencies: ["c4_swift"],
            path: "Sources/CColibri",
            sources: ["swift_storage_bridge.c"],
            publicHeadersPath: "include",
            cSettings: [
                .headerSearchPath("include")
            ]
        ),
        .target(
            name: "Colibri",
            dependencies: ["c4_swift", "CColibriMacOS"],
            path: "Sources/Colibri",
            sources: ["Colibri.swift"],
            linkerSettings: [
                .linkedLibrary("c++")
            ]
        ),
        .testTarget(
            name: "ColibriTests",
            dependencies: ["Colibri"],
            path: "Tests"
        )
    ]
)
EOF

# Zeige Package-Inhalt
echo ""
echo "📋 iOS Package Inhalt:"
find "$iOS_PACKAGE_DIR" -type f \( -name "*.swift" -o -name "*.h" -o -name "Package.swift" -o -name "README.md" -o -name "doc.md" \) | sort

echo ""
echo "📊 XCFramework info:"
if [[ -d "$iOS_PACKAGE_DIR/c4_swift.xcframework" ]]; then
    framework_count=$(find "$iOS_PACKAGE_DIR/c4_swift.xcframework" -name "*.framework" | wc -l | xargs)
    framework_size=$(du -sh "$iOS_PACKAGE_DIR/c4_swift.xcframework" | cut -f1)
    echo "  📱 Frameworks: $framework_count (iOS Device + Simulator)"
    echo "  📏 Größe: $framework_size"
    echo "  📂 Pfad: $iOS_PACKAGE_DIR/c4_swift.xcframework"
fi

echo ""
echo "🎉 iOS Distribution Package erfolgreich erstellt!"
echo "📦 Pfad: $iOS_PACKAGE_DIR"
echo ""
echo "💡 Verwendung:"
echo "   • Für lokale Tests: cd test_ios_app && swift build"
echo "   • Für Distribution: Kopiere ios_package/* nach colibri-stateless-swift Repository"
echo "   • Für iOS Apps: .package(url: \"https://github.com/corpus-core/colibri-stateless-swift.git\")"