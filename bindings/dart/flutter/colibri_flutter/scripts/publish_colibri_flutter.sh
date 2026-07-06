#!/usr/bin/env bash
# Build native libraries and publish colibri_flutter to pub.dev.
#
# Publishes from a temp copy so that:
# - bindings/dart/.pubignore (flutter/) does not hide the package in the monorepo
# - gitignored native binaries (jniLibs, xcframework) are included
#
# Prerequisites:
#   - Android NDK (auto-detected from ~/Library/Android/sdk/ndk) or ANDROID_NDK_HOME
#   - macOS with Xcode (for iOS XCFramework and macOS libcolibri.dylib)
#   - dart pub login (authenticated with pub.dev)
#
# Usage:
#   ./scripts/publish_colibri_flutter.sh           # build + publish
#   ./scripts/publish_colibri_flutter.sh --dry-run  # build + dry-run publish
#   ./scripts/publish_colibri_flutter.sh --skip-build --dry-run  # verify + dry-run only
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

skip_build=false
pub_args=()
for arg in "$@"; do
    case "$arg" in
        --skip-build) skip_build=true ;;
        *) pub_args+=("$arg") ;;
    esac
done

echo "=== colibri_flutter publish ==="
echo ""

# Step 1: Build native libraries
if $skip_build; then
    echo "Step 1: Skipping native build (--skip-build)."
else
    echo "Step 1: Building native libraries..."
    "$SCRIPT_DIR/build_native_libs.sh" --all
fi
echo ""

# Step 2: Verify binaries exist
echo "Step 2: Verifying native binaries..."
missing=false

for abi in arm64-v8a armeabi-v7a x86_64; do
    so="$PLUGIN_DIR/android/src/main/jniLibs/$abi/libcolibri.so"
    if [[ -f "$so" ]]; then
        echo "  OK: $abi/libcolibri.so ($(du -h "$so" | cut -f1))"
    else
        echo "  MISSING: $abi/libcolibri.so"
        missing=true
    fi
done

xcfw="$PLUGIN_DIR/ios/Frameworks/c4_swift.xcframework"
if [[ -d "$xcfw" ]]; then
    echo "  OK: c4_swift.xcframework ($(du -sh "$xcfw" | cut -f1))"
else
    echo "  MISSING: c4_swift.xcframework"
    missing=true
fi

macos_dylib="$PLUGIN_DIR/macos/colibri_flutter/Frameworks/libcolibri.dylib"
macos_xcfw="$PLUGIN_DIR/macos/colibri_flutter/Frameworks/libcolibri.xcframework"
if [[ "$(uname)" == "Darwin" ]]; then
    if [[ -f "$macos_dylib" ]]; then
        echo "  OK: macos/libcolibri.dylib ($(du -h "$macos_dylib" | cut -f1))"
        symbol_count="$(nm -g "$macos_dylib" 2>/dev/null | grep -c 'c4_rpc_set_min_latest_block_ts' || true)"
        if [[ "$symbol_count" -eq 0 ]]; then
            echo "  STALE: macos/libcolibri.dylib missing c4_rpc_set_min_latest_block_ts (rebuild with --macos)"
            missing=true
        fi
    else
        echo "  MISSING: macos/libcolibri.dylib"
        missing=true
    fi
    if [[ -d "$macos_xcfw" ]]; then
        echo "  OK: macos/libcolibri.xcframework ($(du -sh "$macos_xcfw" | cut -f1))"
    else
        echo "  MISSING: macos/libcolibri.xcframework (required for Swift Package Manager)"
        missing=true
    fi
fi

if $missing; then
    echo ""
    echo "Error: Some native binaries are missing. Cannot publish."
    exit 1
fi

echo ""
echo "Step 3: Preparing publish tree..."
rsync -a \
  --exclude='.dart_tool/' \
  --exclude='build/' \
  --exclude='example/.dart_tool/' \
  --exclude='example/build/' \
  --exclude='scripts/' \
  --exclude='example/scripts/' \
  --exclude='pubspec_overrides.yaml' \
  "$PLUGIN_DIR/" "$TMP_DIR/"

echo "Publishing colibri_flutter from $TMP_DIR (monorepo gitignore/pubignore bypassed)"
(cd "$TMP_DIR" && dart pub publish "${pub_args[@]}")
