#!/usr/bin/env bash
# Publish colibri_stateless to pub.dev from a copy that excludes flutter/
# so the package stays small and the parent .pubignore does not affect the Flutter package.
#
# Package version is set to the Colibri release version (same as npm):
#   C4_VERSION / COLIBRI_VERSION, or the exact git tag on HEAD.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=colibri_version.sh
source "$SCRIPT_DIR/colibri_version.sh"

DART_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
V="$(c4_resolve_package_version --exact)"
echo "Publishing colibri_stateless $V (Colibri / npm version)"

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

rsync -a --exclude='flutter/' --exclude='.dart_tool/' --exclude='.git/' \
  "$DART_DIR/" "$TMP_DIR/"

c4_apply_version_to_pubspec "$TMP_DIR/pubspec.yaml" "$V"

echo "Publishing colibri_stateless from $TMP_DIR (flutter/ excluded, version $V)"
(cd "$TMP_DIR" && dart pub publish "$@")
