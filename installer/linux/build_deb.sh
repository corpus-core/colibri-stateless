#!/bin/bash
# Build Debian package for Colibri Server

set -e

VERSION="${1:-1.0.0}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "Building Colibri Server Debian package..."
echo "Version: $VERSION"
echo "Project root: $PROJECT_ROOT"

# Check if required tools are installed
if ! command -v dpkg-buildpackage &> /dev/null; then
    echo "Error: dpkg-buildpackage not found. Install with:"
    echo "  sudo apt-get install dpkg-dev debhelper"
    exit 1
fi

# Copy debian directory to project root (dpkg-buildpackage expects it there)
echo "Copying debian directory to project root..."
rm -rf "$PROJECT_ROOT/debian"
cp -r "$SCRIPT_DIR/debian" "$PROJECT_ROOT/debian"

# Update version in debian/changelog
echo "Updating version in debian/changelog to $VERSION..."
sed -i "s/^colibri-server (.*)/colibri-server ($VERSION-1)/" "$PROJECT_ROOT/debian/changelog"

# Make scripts executable
chmod +x "$PROJECT_ROOT/debian/rules"
chmod +x "$PROJECT_ROOT/debian/postinst"
chmod +x "$PROJECT_ROOT/debian/prerm"
chmod +x "$PROJECT_ROOT/debian/postrm"

# Build the package
cd "$PROJECT_ROOT"
dpkg-buildpackage -us -uc -b

# Copy .deb to installer/linux for easier upload
cp ../colibri-server_*.deb "$SCRIPT_DIR/" || true

# Clean up temporary debian directory
rm -rf "$PROJECT_ROOT/debian"

echo ""
echo "====================================================================="
echo "Debian package built successfully!"
echo "Package: $SCRIPT_DIR/colibri-server_${VERSION}-1_*.deb"
echo ""
echo "To install: sudo dpkg -i colibri-server_*.deb"
echo "            sudo apt-get install -f  # Install dependencies"
echo "====================================================================="

