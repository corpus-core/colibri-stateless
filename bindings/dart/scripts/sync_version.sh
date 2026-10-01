#!/usr/bin/env bash
# Sync Dart and Flutter package versions to the Colibri release version
# (same source as npm: C4_VERSION / git tag, see colibri_version.sh).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=colibri_version.sh
source "$SCRIPT_DIR/colibri_version.sh"

DART_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FLUTTER_DIR="$DART_DIR/flutter/colibri_flutter"

V="$(c4_resolve_package_version "${1:-}")"
echo "Resolved Colibri package version: $V"

c4_apply_version_to_pubspec "$DART_DIR/pubspec.yaml" "$V"
c4_apply_version_to_pubspec "$FLUTTER_DIR/pubspec.yaml" "$V"

EXAMPLE_PUBSPEC="$FLUTTER_DIR/example/pubspec.yaml"
if [[ -f "$EXAMPLE_PUBSPEC" ]]; then
  # Flutter apps use version+build; keep build number if present.
  if grep -qE '^version:[[:space:]]*[0-9]' "$EXAMPLE_PUBSPEC"; then
    build="$(grep -E '^version:' "$EXAMPLE_PUBSPEC" | sed -n 's/.*+\([0-9][0-9]*\).*/\1/p')"
    [[ -z "$build" ]] && build=1
    sed -i.bak "s/^version: .*$/version: ${V}+${build}/" "$EXAMPLE_PUBSPEC"
    rm -f "$EXAMPLE_PUBSPEC.bak"
  fi
fi

echo "Updated:"
echo "  bindings/dart/pubspec.yaml → $V"
echo "  bindings/dart/flutter/colibri_flutter/pubspec.yaml → $V (colibri_stateless: ^$V)"
[[ -f "$EXAMPLE_PUBSPEC" ]] && echo "  example/pubspec.yaml → ${V}+*"
echo ""
echo "Hint: for a release publish, checkout the release tag or set C4_VERSION=$V"
echo "  ./scripts/sync_version.sh --exact   # require exact tag / env"
