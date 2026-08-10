#!/usr/bin/env bash
# Publish colibri_stateless to pub.dev from a copy that excludes flutter/
# so the package stays small and the parent .pubignore does not affect the Flutter package.
set -euo pipefail

DART_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

rsync -a --exclude='flutter/' --exclude='.dart_tool/' --exclude='.git/' \
  "$DART_DIR/" "$TMP_DIR/"

# Use same .pubignore but ensure flutter is excluded in the copy (already omitted by rsync)
echo "Publishing colibri_stateless from $TMP_DIR (flutter/ excluded)"
(cd "$TMP_DIR" && dart pub publish "$@")
