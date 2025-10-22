#!/bin/bash
# Uninstall script for Colibri Server on macOS
# This script removes all files installed by the Colibri Server package

set -e

echo "=========================================="
echo "Colibri Server Uninstaller for macOS"
echo "=========================================="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo "Error: This script must be run as root (use sudo)"
    exit 1
fi

# Function to safely remove a file
remove_file() {
    if [ -f "$1" ]; then
        echo "Removing file: $1"
        rm -f "$1"
    else
        echo "File not found (skipping): $1"
    fi
}

# Function to safely remove a directory
remove_directory() {
    if [ -d "$1" ]; then
        echo "Removing directory: $1"
        rm -rf "$1"
    else
        echo "Directory not found (skipping): $1"
    fi
}

# Stop and unload the LaunchDaemon if it's running
echo ""
echo "Stopping Colibri Server service..."
if launchctl list | grep -q "io.corpuscore.colibri-server"; then
    launchctl unload /Library/LaunchDaemons/io.corpuscore.colibri-server.plist 2>/dev/null || true
    echo "Service stopped successfully"
else
    echo "Service is not running"
fi

# Remove LaunchDaemon plist
echo ""
remove_file "/Library/LaunchDaemons/io.corpuscore.colibri-server.plist"

# Remove binaries
echo ""
remove_file "/usr/local/bin/colibri-server"
remove_file "/usr/local/bin/colibri-prover"
remove_file "/usr/local/bin/colibri-verifier"
remove_file "/usr/local/bin/colibri-ssz"

# Ask user if they want to remove configuration and logs
echo ""
read -p "Do you want to remove configuration files and logs? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    remove_directory "/usr/local/etc/colibri"
    remove_file "/var/log/colibri-server.log"
    remove_file "/var/log/colibri-server.error.log"
    remove_directory "/var/lib/colibri"
    echo "Configuration and logs removed"
else
    echo "Configuration and logs preserved at:"
    echo "  - /usr/local/etc/colibri/"
    echo "  - /var/log/colibri-server.log"
    echo "  - /var/log/colibri-server.error.log"
fi

# Remove package receipt (so the system knows it's uninstalled)
echo ""
PACKAGE_ID="io.corpuscore.colibri-server"
if pkgutil --pkg-info "$PACKAGE_ID" >/dev/null 2>&1; then
    echo "Removing package receipt..."
    pkgutil --forget "$PACKAGE_ID"
    echo "Package receipt removed"
else
    echo "Package receipt not found (already removed)"
fi

echo ""
echo "=========================================="
echo "Colibri Server has been uninstalled!"
echo "=========================================="

