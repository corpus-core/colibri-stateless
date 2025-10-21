#!/bin/bash
# Build Debian package for Colibri Server

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "Building Colibri Server Debian package..."
echo "Project root: $PROJECT_ROOT"

# Check if required tools are installed
if ! command -v dpkg-buildpackage &> /dev/null; then
    echo "Error: dpkg-buildpackage not found. Install with:"
    echo "  sudo apt-get install dpkg-dev debhelper"
    exit 1
fi

# Make scripts executable
chmod +x "$PROJECT_ROOT/installer/linux/debian/rules"
chmod +x "$PROJECT_ROOT/installer/linux/debian/postinst"
chmod +x "$PROJECT_ROOT/installer/linux/debian/prerm"
chmod +x "$PROJECT_ROOT/installer/linux/debian/postrm"

# Build the package
cd "$PROJECT_ROOT"
dpkg-buildpackage -us -uc -b

echo ""
echo "====================================================================="
echo "Debian package built successfully!"
echo "Package files are in: $(dirname $PROJECT_ROOT)"
echo ""
echo "To install: sudo dpkg -i ../colibri-server_*.deb"
echo "            sudo apt-get install -f  # Install dependencies"
echo "====================================================================="

