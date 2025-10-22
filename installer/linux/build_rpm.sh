#!/bin/bash
# Build RPM package for Colibri Server

set -e

VERSION="${1:-1.0.0}"

# RPM doesn't allow + or - in version, so split into VERSION and RELEASE
# e.g., "0.0.0+devabcdef" -> VERSION="0.0.0" RELEASE="0.devabcdef"
if [[ "$VERSION" == *"+"* ]]; then
    # Split at + sign
    RPM_VERSION="${VERSION%%+*}"
    RPM_RELEASE="0.${VERSION##*+}"
else
    RPM_VERSION="$VERSION"
    RPM_RELEASE="1"
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "Building Colibri Server RPM package..."
echo "Version: $RPM_VERSION"
echo "Release: $RPM_RELEASE"
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

# Create source tarball using RPM_VERSION
TARBALL="$RPMBUILD_DIR/SOURCES/colibri-server-${RPM_VERSION}.tar.gz"
echo "Creating source tarball: $TARBALL"
cd "$PROJECT_ROOT/.."
tar --exclude='.git' --exclude='build' --exclude='node_modules' \
    --transform="s|^colibri-stateless|colibri-server-${RPM_VERSION}|" \
    -czf "$TARBALL" colibri-stateless

# Copy and update spec file
cp "$PROJECT_ROOT/installer/linux/rpm/colibri-server.spec" "$RPMBUILD_DIR/SPECS/"
echo "Updating version to $RPM_VERSION and release to $RPM_RELEASE..."
sed -i "s/^Version:.*/Version:        $RPM_VERSION/" "$RPMBUILD_DIR/SPECS/colibri-server.spec"
sed -i "s/^Release:.*/Release:        $RPM_RELEASE%{?dist}/" "$RPMBUILD_DIR/SPECS/colibri-server.spec"

# Build the package
echo "Building RPM package..."
cd "$RPMBUILD_DIR"
rpmbuild -bb SPECS/colibri-server.spec

# Copy .rpm to installer/linux for easier upload
cp RPMS/*/colibri-server-*.rpm "$SCRIPT_DIR/" || true

echo ""
echo "====================================================================="
echo "RPM package built successfully!"
echo "Package: $SCRIPT_DIR/colibri-server-${RPM_VERSION}-${RPM_RELEASE}.*.rpm"
echo ""
echo "To install: sudo rpm -ivh colibri-server-*.rpm"
echo "Or:         sudo dnf install colibri-server-*.rpm"
echo "====================================================================="

