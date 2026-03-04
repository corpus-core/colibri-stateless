#!/usr/bin/env bash
# Run the same steps as the Dart/Flutter CI workflow locally.
# Usage: from repo root: ./bindings/dart/run_ci_locally.sh
#    or: from bindings/dart: ./run_ci_locally.sh (script will cd to repo root)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT"

echo "=== 1. Build native library (DART=ON) ==="
cmake -S . -B build-dart -DDART=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-dart --target colibri_dart
ls -la bindings/dart/native/ || true

echo ""
echo "=== 2. Dart tests ==="
cd bindings/dart
dart pub get
dart test

echo ""
echo "=== 3. Flutter tests (colibri_flutter plugin + client integration) ==="
cd flutter/colibri_flutter
# Point to the native lib we built so client integration tests can run
if [ -f "$REPO_ROOT/bindings/dart/native/libcolibri.dylib" ]; then
  export COLIBRI_DART_LIBRARY="$REPO_ROOT/bindings/dart/native/libcolibri.dylib"
elif [ -f "$REPO_ROOT/bindings/dart/native/libcolibri.so" ]; then
  export COLIBRI_DART_LIBRARY="$REPO_ROOT/bindings/dart/native/libcolibri.so"
elif [ -f "$REPO_ROOT/bindings/dart/native/colibri.dll" ]; then
  export COLIBRI_DART_LIBRARY="$REPO_ROOT/bindings/dart/native/colibri.dll"
fi
flutter pub get
flutter test

echo ""
echo "All CI steps completed successfully."
