#!/usr/bin/env bash
# Build macOS libcolibri.dylib + libcolibri.xcframework for SPM and CocoaPods.
#
# Usage (from plugin root):
#   ./scripts/build_macos_xcframework.sh
#   BUILD_MACOS_UNIVERSAL=1 ./scripts/build_macos_xcframework.sh  # arm64 + x86_64 slices
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

exec bash "$ROOT_DIR/scripts/build_flutter_binaries.sh" --macos
