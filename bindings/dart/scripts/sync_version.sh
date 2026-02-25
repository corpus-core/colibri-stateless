#!/usr/bin/env bash
# Sync Dart and Flutter package versions from the repository root VERSION file.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DART_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_VERSION="${DART_DIR}/../../VERSION"

if [[ ! -f "$ROOT_VERSION" ]]; then
  echo "VERSION file not found at $ROOT_VERSION" >&2
  exit 1
fi

V="$(cat "$ROOT_VERSION" | tr -d '\n' | tr -d ' ')"

# Update bindings/dart/pubspec.yaml
sed -i.bak "s/^version: .*$/version: $V/" "$DART_DIR/pubspec.yaml" && rm -f "$DART_DIR/pubspec.yaml.bak"

# Update bindings/dart/flutter/colibri_flutter/pubspec.yaml
FLUTTER_PUBSPEC="$DART_DIR/flutter/colibri_flutter/pubspec.yaml"
sed -i.bak "s/^version: .*$/version: $V/" "$FLUTTER_PUBSPEC" && rm -f "$FLUTTER_PUBSPEC.bak"

echo "Updated Dart and Flutter package versions to $V (from repo root VERSION)."
