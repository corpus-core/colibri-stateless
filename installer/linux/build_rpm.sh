#!/bin/bash
# Build RPM package for Colibri Server

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
VERSION="1.0.0"

echo "Building Colibri Server RPM package..."
echo "Project root: $PROJECT_ROOT"

# Check if required tools are installed
if ! command -v rpmbuild &> /dev/null; then
    echo "Error: rpmbuild not found. Install with:"
    echo "  sudo dnf install rpm-build rpmdevtools  # Fedora/RHEL 8+"
    echo "  sudo yum install rpm-build rpmdevtools  # RHEL/CentOS 7"
    exit 1
fi

# Setup RPM build environment
RPMBUILD_DIR="$HOME/rpmbuild"
mkdir -p "$RPMBUILD_DIR"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

# Create source tarball
TARBALL="$RPMBUILD_DIR/SOURCES/colibri-server-${VERSION}.tar.gz"
echo "Creating source tarball: $TARBALL"
cd "$PROJECT_ROOT/.."
tar --exclude='.git' --exclude='build' --exclude='node_modules' \
    --transform="s|^colibri-stateless|colibri-server-${VERSION}|" \
    -czf "$TARBALL" colibri-stateless

# Copy spec file
cp "$PROJECT_ROOT/installer/linux/rpm/colibri-server.spec" "$RPMBUILD_DIR/SPECS/"

# Build the package
echo "Building RPM package..."
cd "$RPMBUILD_DIR"
rpmbuild -bb SPECS/colibri-server.spec

echo ""
echo "====================================================================="
echo "RPM package built successfully!"
echo "Package location: $RPMBUILD_DIR/RPMS/"
echo ""
echo "To install: sudo rpm -ivh $RPMBUILD_DIR/RPMS/*/colibri-server-*.rpm"
echo "Or:         sudo dnf install $RPMBUILD_DIR/RPMS/*/colibri-server-*.rpm"
echo "====================================================================="

