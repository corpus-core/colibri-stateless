#!/usr/bin/env bash
# Increment the build number (4th part: X.Y.Z+BUILD) in pubspec.yaml.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PUBSPEC="${SCRIPT_DIR}/../pubspec.yaml"

if [[ ! -f "$PUBSPEC" ]]; then
  echo "pubspec.yaml not found: $PUBSPEC" >&2
  exit 1
fi

# Read current version line (e.g. "version: 0.0.1+5" or "version: 0.0.1")
current=$(grep -E '^version:\s*' "$PUBSPEC" | sed 's/^version:\s*//' | tr -d '\r')
if [[ -z "$current" ]]; then
  echo "No version line in pubspec.yaml" >&2
  exit 1
fi

# Increment build: version is X.Y.Z or X.Y.Z+BUILD
if [[ "$current" == *+* ]]; then
  base="${current%+*}"
  build="${current##*+}"
  build=$((build + 1))
  new_version="${base}+${build}"
else
  new_version="${current}+1"
fi

# Replace in file (works on macOS and Linux)
if sed --version 2>/dev/null | grep -q GNU; then
  sed -i "s/^version:.*/version: $new_version/" "$PUBSPEC"
else
  sed -i.bak "s/^version:.*/version: $new_version/" "$PUBSPEC" && rm -f "${PUBSPEC}.bak"
fi

echo "Build number bumped: $current → $new_version"
