#!/usr/bin/env bash
# Build native libraries and publish colibri_flutter to pub.dev.
#
# Prerequisites:
#   - ANDROID_NDK_HOME set (for Android .so files)
#   - macOS with Xcode (for iOS XCFramework)
#   - dart pub login (authenticated with pub.dev)
#
# Usage:
#   ./scripts/publish_colibri_flutter.sh           # build + publish
#   ./scripts/publish_colibri_flutter.sh --dry-run  # build + dry-run publish
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "=== colibri_flutter publish ==="
echo ""

# Step 1: Build native libraries
echo "Step 1: Building native libraries..."
"$SCRIPT_DIR/build_native_libs.sh" --all
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

if $missing; then
    echo ""
    echo "Error: Some native binaries are missing. Cannot publish."
    exit 1
fi

echo ""
echo "Step 3: Publishing to pub.dev..."

# Copy package to a temp directory outside the monorepo so parent .gitignore
# rules don't hide the native binaries from dart pub publish.
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

rsync -a \
  --exclude='.dart_tool/' \
  --exclude='.git/' \
  --exclude='build/' \
  --exclude='pubspec_overrides.yaml' \
  "$PLUGIN_DIR/" "$TMP_DIR/"

echo "Publishing colibri_flutter from $TMP_DIR (outside monorepo .gitignore)"
(cd "$TMP_DIR" && dart pub publish "$@")
