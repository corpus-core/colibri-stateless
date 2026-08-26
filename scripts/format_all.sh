#!/usr/bin/env bash
#
# Format all C sources under src/ with clang-format, using the
# .clang-format file at the repository root.
#
# Usage:
#   ./scripts/format_all.sh
#

set -euo pipefail

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SRC_DIR="$PROJECT_ROOT/src"
CLANG_FORMAT_FILE="$PROJECT_ROOT/.clang-format"

if ! command -v clang-format >/dev/null 2>&1; then
  echo "Error: clang-format is not installed" >&2
  echo "  macOS:  brew install clang-format" >&2
  echo "  Ubuntu: sudo apt-get install clang-format" >&2
  exit 1
fi

if [[ ! -f "$CLANG_FORMAT_FILE" ]]; then
  echo "Error: .clang-format not found at $CLANG_FORMAT_FILE" >&2
  exit 1
fi

if [[ ! -d "$SRC_DIR" ]]; then
  echo "Error: source directory not found at $SRC_DIR" >&2
  exit 1
fi

count=$(find "$SRC_DIR" \( -name '*.c' -o -name '*.h' \) | wc -l | tr -d ' ')

if [[ "$count" -eq 0 ]]; then
  echo "No .c or .h files found under $SRC_DIR"
  exit 0
fi

find "$SRC_DIR" \( -name '*.c' -o -name '*.h' \) \
  -exec clang-format -i --style=file:"$CLANG_FORMAT_FILE" {} +

echo "Formatted $count files under $SRC_DIR"
