#!/usr/bin/env bash
# Build native libraries and publish colibri_flutter to pub.dev.
#
# Publishes from a temporary copy so monorepo ignore rules do not empty the
# package:
#   - bindings/dart/.pubignore has `flutter/` (hides this package in-tree)
#   - root .gitignore excludes jniLibs/ and the iOS XCFramework
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

xcfw="$PLUGIN_DIR/ios/colibri_flutter/Frameworks/c4_swift.xcframework"
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

# Step 3: Publish from an isolated copy (avoids parent .pubignore / gitignore)
echo ""
echo "Step 3: Publishing to pub.dev from isolated copy..."
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

rsync -a \
  --exclude='.dart_tool/' \
  --exclude='build/' \
  --exclude='scripts/' \
  --exclude='example/scripts/' \
  --exclude='pubspec_overrides.yaml' \
  --exclude='.pubignore' \
  "$PLUGIN_DIR/" "$TMP_DIR/"

# Ensure LICENSE / README / CHANGELOG / natives are present in the copy
for f in LICENSE README.md CHANGELOG.md pubspec.yaml; do
  if [[ ! -f "$TMP_DIR/$f" ]]; then
    echo "Error: missing $f in publish copy"
    exit 1
  fi
done
for abi in arm64-v8a armeabi-v7a x86_64; do
  if [[ ! -f "$TMP_DIR/android/src/main/jniLibs/$abi/libcolibri.so" ]]; then
    echo "Error: missing jniLibs/$abi/libcolibri.so in publish copy"
    exit 1
  fi
done
if [[ ! -d "$TMP_DIR/ios/colibri_flutter/Frameworks/c4_swift.xcframework" ]]; then
  echo "Error: missing c4_swift.xcframework in publish copy"
  exit 1
fi

echo "Publishing colibri_flutter from $TMP_DIR"
(cd "$TMP_DIR" && dart pub publish "$@")
