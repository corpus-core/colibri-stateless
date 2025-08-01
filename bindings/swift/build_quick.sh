#!/bin/bash

# Swift Quick Build Script für lokale Entwicklung
# Baut nur für die aktuelle Architektur (viel schneller)
# Copyright (c) 2025 corpus.core

set -e

echo "🚀 Starte Swift Quick Build (aktuelle Architektur)..."

# Variablen
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SWIFT_DIR="$ROOT_DIR/bindings/swift"
BUILD_DIR="$ROOT_DIR/build_quick"

# Cleanup alter Build
echo "🧹 Cleanup alter Build..."
rm -rf "$BUILD_DIR"

# Prüfe ob wir auf macOS sind
if [[ "$(uname)" != "Darwin" ]]; then
    echo "❌ Fehler: Dieses Script funktioniert nur auf macOS"
    exit 1
fi

# Erkenne aktuelle Architektur
ARCH=$(uname -m)
if [[ "$ARCH" == "arm64" ]]; then
    TARGET_ARCH="arm64"
    SDK_NAME="macosx"
    echo "🔧 Erkannte Architektur: Apple Silicon (arm64)"
elif [[ "$ARCH" == "x86_64" ]]; then
    TARGET_ARCH="x86_64"
    SDK_NAME="macosx"
    echo "🔧 Erkannte Architektur: Intel (x86_64)"
else
    echo "❌ Unbekannte Architektur: $ARCH"
    exit 1
fi

# SDK Pfad ermitteln
SDK_PATH=$(xcrun --sdk "$SDK_NAME" --show-sdk-path)
echo "📱 SDK: $SDK_PATH"

# Build für aktuelle Architektur (macOS statt iOS für einfachere Entwicklung)
echo "🛠️  Baue für $TARGET_ARCH (macOS)..."
cd "$ROOT_DIR"
cmake \
    -DSWIFT=true \
    -DCMAKE_OSX_ARCHITECTURES="$TARGET_ARCH" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 \
    -B "$BUILD_DIR" \
    .

cd "$BUILD_DIR"
make -j$(sysctl -n hw.ncpu)

# Prüfe ob Library erstellt wurde
STATIC_LIB="$BUILD_DIR/bindings/swift/libc4_swift_binding.a"
if [[ -f "$STATIC_LIB" ]]; then
    echo "✅ Static Library erfolgreich erstellt: $STATIC_LIB"
    
    # Zeige Library-Info
    echo "📊 Library-Info:"
    file "$STATIC_LIB"
    
    # Größe anzeigen
    echo "📏 Library-Größe:"
    du -sh "$STATIC_LIB"
else
    echo "❌ Fehler: Static Library wurde nicht erstellt"
    exit 1
fi

# Swift Package für Development vorbereiten
echo "📦 Bereite Swift Package für Development vor..."
cd "$SWIFT_DIR"

# Erstelle vereinfachte Package.swift für Quick Development
cat > Package_quick.swift << 'EOF'
// swift-tools-version:5.3
import PackageDescription

let package = Package(
    name: "Colibri",
    platforms: [.macOS(.v10_15)],
    products: [
        .library(name: "Colibri", targets: ["Colibri"])
    ],
    targets: [
        .target(
            name: "Colibri",
            path: "src",
            publicHeadersPath: "include",
            cSettings: [
                .headerSearchPath("include"),
                .define("C4_DEVELOPMENT_BUILD")
            ],
            linkerSettings: [
                .linkedLibrary("c++"),
                .unsafeFlags([
                    "../../build_quick/bindings/swift/libc4_swift_binding.a",
                    "../../build_quick/src/util/libutil.a",
                    "../../build_quick/src/proofer/libproofer.a", 
                    "../../build_quick/src/verifier/libverifier.a",
                    "../../build_quick/libs/crypto/libcrypto.a",
                    "../../build_quick/libs/blst/libblst.a"
                ])
            ]),
        .testTarget(
            name: "ColibriTests",
            dependencies: ["Colibri"],
            path: "Tests"
        )
    ]
)
EOF

# Test-Build
echo "🧪 Teste Swift Package..."
if swift build -c debug --package-path . --build-path "$BUILD_DIR/swift_build" &>/dev/null; then
    echo "✅ Swift Package Development Build erfolgreich"
else
    echo "⚠️  Swift Package Build fehlgeschlagen - prüfen Sie die Abhängigkeiten"
fi

echo ""
echo "🎉 Quick Build erfolgreich abgeschlossen!"
echo "📦 Static Library: $STATIC_LIB"
echo "📝 Quick Package: $SWIFT_DIR/Package_quick.swift"
echo ""
echo "💡 Für lokale Entwicklung:"
echo "   1. Verwenden Sie Package_quick.swift für schnelle Builds"
echo "   2. Für Production verwenden Sie build_local.sh für XCFramework"
echo "   3. Tests: swift test --package-path . --build-path $BUILD_DIR/swift_build"