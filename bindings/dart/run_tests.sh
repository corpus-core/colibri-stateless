#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FLUTTER_EXAMPLE="$ROOT_DIR/flutter/colibri_flutter/example"

echo "=== Dart tests ==="
cd "$ROOT_DIR"
dart test

if [ -d "$FLUTTER_EXAMPLE/test" ]; then
  echo ""
  echo "=== Flutter widget tests ==="
  cd "$FLUTTER_EXAMPLE"
  flutter test
fi

echo ""
echo "All tests passed."
