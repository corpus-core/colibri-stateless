#!/usr/bin/env bash
# Bump build number, then run the app. Use this instead of `flutter run` to auto-increment build.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

"$SCRIPT_DIR/scripts/bump_build.sh"
exec flutter run "$@"
