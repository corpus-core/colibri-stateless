#!/usr/bin/env bash
# Publish colibri_stateless to pub.dev from a copy that excludes flutter/ and ffi/.
# Replaces path dependency on colibri_stateless_ffi with hosted (pub.dev does not allow path deps).
# Prerequisite: publish colibri_stateless_ffi first (see bindings/dart/ffi/ and its publish_to).
set -euo pipefail

DART_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION=$(grep '^version:' "$DART_DIR/pubspec.yaml" | sed 's/version: *//')
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

rsync -a --exclude='flutter/' --exclude='ffi/' --exclude='.dart_tool/' --exclude='.git/' \
  "$DART_DIR/" "$TMP_DIR/"

# Replace path dependency with hosted so the published package resolves colibri_stateless_ffi from pub.dev
perl -i.bak -0pe 's/colibri_stateless_ffi:\s*\n\s*path: ffi/colibri_stateless_ffi: ^'"${VERSION}"'/g' "$TMP_DIR/pubspec.yaml" && rm -f "$TMP_DIR/pubspec.yaml.bak"

echo "Publishing colibri_stateless from $TMP_DIR (flutter/ and ffi/ excluded; colibri_stateless_ffi: ^$VERSION)"
(cd "$TMP_DIR" && dart pub publish "$@")
