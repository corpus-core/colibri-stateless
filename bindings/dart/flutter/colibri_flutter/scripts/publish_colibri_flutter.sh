#!/usr/bin/env bash
# Build native libraries and publish colibri_flutter to pub.dev.
#
# Publishes from a temporary copy so monorepo ignore rules do not empty the
# package:
#   - bindings/dart/.pubignore has `flutter/` (hides this package in-tree)
#   - root .gitignore excludes jniLibs/ and the iOS XCFramework
#
# Package version (and colibri_stateless dependency) is set to the Colibri
# release version (same as npm): C4_VERSION / COLIBRI_VERSION, or exact git tag.
#
# Prerequisites:
#   - ANDROID_NDK_HOME set (for Android .so files)
#   - macOS with Xcode (for iOS XCFramework)
#   - dart pub login (authenticated with pub.dev)
#   - colibri_stateless already published at the same version (or compatible ^)
#
# Usage:
#   ./scripts/publish_colibri_flutter.sh           # build + publish
#   ./scripts/publish_colibri_flutter.sh --dry-run  # build + dry-run publish
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
DART_SCRIPTS="$(cd "$PLUGIN_DIR/../../scripts" && pwd)"
# shellcheck source=../../scripts/colibri_version.sh
source "$DART_SCRIPTS/colibri_version.sh"

V="$(c4_resolve_package_version --exact)"
PLUGIN_V="$(awk '/^version:[[:space:]]*/ { print $2; exit }' "$PLUGIN_DIR/pubspec.yaml")"
if [[ -z "$PLUGIN_V" ]]; then
  PLUGIN_V="$V"
fi
echo "=== colibri_flutter publish (plugin $PLUGIN_V, Colibri core $V) ==="
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

macos_dylib="$PLUGIN_DIR/macos/colibri_flutter/Frameworks/libcolibri.dylib"
macos_xcfw="$PLUGIN_DIR/macos/colibri_flutter/Frameworks/libcolibri.xcframework"
if [[ -f "$macos_dylib" ]]; then
    if nm -gU "$macos_dylib" 2>/dev/null | grep -q ' _c4_reset_caches$'; then
        echo "  OK: macOS libcolibri.dylib ($(du -h "$macos_dylib" | cut -f1), exports c4_reset_caches)"
    else
        echo "  INVALID: macOS libcolibri.dylib missing c4_reset_caches export"
        missing=true
    fi
else
    echo "  MISSING: macOS libcolibri.dylib"
    missing=true
fi
if [[ -d "$macos_xcfw" ]]; then
    echo "  OK: macOS libcolibri.xcframework ($(du -sh "$macos_xcfw" | cut -f1))"
else
    echo "  MISSING: macOS libcolibri.xcframework"
    missing=true
fi

if $missing; then
    echo ""
    echo "Error: Some native binaries are missing. Cannot publish."
    exit 1
fi

# Step 3: Publish from an isolated copy (avoids parent .pubignore / gitignore)
echo ""
echo "Step 3: Publishing to pub.dev from isolated copy (version $PLUGIN_V)..."
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

c4_apply_version_to_pubspec "$TMP_DIR/pubspec.yaml" "$PLUGIN_V" "$V"

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
if [[ ! -f "$TMP_DIR/macos/colibri_flutter/Frameworks/libcolibri.dylib" ]]; then
  echo "Error: missing macOS libcolibri.dylib in publish copy"
  exit 1
fi
if [[ ! -f "$TMP_DIR/macos/colibri_flutter/Package.swift" ]]; then
  echo "Error: missing macOS Package.swift in publish copy"
  exit 1
fi

if ! grep -qE "^##[[:space:]]*${PLUGIN_V}([[:space:]]|\$)" "$TMP_DIR/CHANGELOG.md"; then
  echo "Warning: CHANGELOG.md has no '## $PLUGIN_V' section — pub.dev may warn." >&2
fi

echo "Publishing colibri_flutter $PLUGIN_V from $TMP_DIR"
(cd "$TMP_DIR" && dart pub publish "$@")
