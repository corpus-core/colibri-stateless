#!/bin/bash
# Build macOS installer package for Colibri Server

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Accept version as first argument, default to 1.0.0
VERSION="${1:-1.0.0}"

echo "Building Colibri Server macOS installer package..."
echo "Project root: $PROJECT_ROOT"
echo "Version: $VERSION"

# Create build directory
BUILD_DIR="$PROJECT_ROOT/build/macos-installer"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"/{root,scripts}

# Build the server binary
echo "Building server binary..."
cd "$PROJECT_ROOT"
mkdir -p build/macos-release
cd build/macos-release
cmake -DCMAKE_BUILD_TYPE=Release \
    -DHTTP_SERVER=ON \
    -DPROVER=ON \
    -DVERIFIER=ON \
    -DPROVER_CACHE=ON \
    -DCLI=OFF \
    -DTEST=OFF \
    -DINSTALLER=ON \
    ../..
make -j4 server

# Prepare package contents
echo "Preparing package contents..."
PACKAGE_ROOT="$BUILD_DIR/root"

# Find the server binary (could be in bin/ or default/bin/ depending on CMake setup)
if [ -f "bin/colibri-server" ]; then
    SERVER_BIN="bin/colibri-server"
elif [ -f "default/bin/colibri-server" ]; then
    SERVER_BIN="default/bin/colibri-server"
else
    echo "Error: Cannot find server binary!"
    echo "Checked:"
    echo "  - bin/colibri-server"
    echo "  - default/bin/colibri-server"
    ls -la bin/ 2>/dev/null || echo "  bin/ directory not found"
    ls -la default/bin/ 2>/dev/null || echo "  default/bin/ directory not found"
    exit 1
fi

echo "Found server binary at: $SERVER_BIN"

# Install binary
install -d "$PACKAGE_ROOT/usr/local/bin"
install -m 0755 "$SERVER_BIN" "$PACKAGE_ROOT/usr/local/bin/colibri-server"

# Install config
install -d "$PACKAGE_ROOT/usr/local/etc/colibri"
install -m 0644 "$PROJECT_ROOT/installer/config/server.conf.default" \
    "$PACKAGE_ROOT/usr/local/etc/colibri/server.conf"

# Install LaunchDaemon plist
install -d "$PACKAGE_ROOT/usr/local/share/colibri"
install -m 0644 "$PROJECT_ROOT/installer/scripts/launchd/io.corpuscore.colibri-server.plist" \
    "$PACKAGE_ROOT/usr/local/share/colibri/io.corpuscore.colibri-server.plist"

# Copy scripts
cp "$PROJECT_ROOT/installer/macos/scripts/preinstall" "$BUILD_DIR/scripts/"
cp "$PROJECT_ROOT/installer/macos/scripts/postinstall" "$BUILD_DIR/scripts/"
chmod +x "$BUILD_DIR/scripts/"*

# Build package
echo "Building package..."
OUTPUT_PKG="$PROJECT_ROOT/colibri-server-${VERSION}.pkg"
pkgbuild --root "$PACKAGE_ROOT" \
    --scripts "$BUILD_DIR/scripts" \
    --identifier io.corpuscore.colibri-server \
    --version "$VERSION" \
    --install-location / \
    "$OUTPUT_PKG"

echo ""
echo "====================================================================="
echo "macOS installer package built successfully!"
echo "Package: $OUTPUT_PKG"
echo ""
echo "To install: sudo installer -pkg $OUTPUT_PKG -target /"
echo "Or: Double-click the .pkg file in Finder"
echo "====================================================================="

