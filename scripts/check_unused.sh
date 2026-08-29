#!/bin/bash
# Local / CI unused-symbol checker for src/.
# Same command as the `unused_code` job in .github/workflows/cmake.yml
#
# Usage:
#   ./scripts/check_unused.sh
#   ./scripts/check_unused.sh --json
#   ./scripts/check_unused.sh --fail-on ''   # report only, never fail

set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
exec python3 "$ROOT/scripts/check_unused.py" "$@"
