#!/usr/bin/env bash
# Copy this package into a sibling directory suitable for pushing to a
# mirror repo (e.g. corpus-core/colibri-flutter). Pub.dev expects the
# repository URL to clone to a repo with pubspec.yaml at root.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
MIRROR_DIR="$(cd "$PKG_DIR/../colibri_flutter_mirror" && pwd)"

echo "Copying package to $MIRROR_DIR for mirror repo..."
rsync -a --exclude='.dart_tool/' --exclude='.git/' --exclude='scripts/' \
  "$PKG_DIR/" "$MIRROR_DIR/"

echo "Done. Next steps:"
echo "  1. cd $MIRROR_DIR && git init && git add . && git commit -m 'Mirror for pub.dev'"
echo "  2. Create repo corpus-core/colibri-flutter on GitHub and push"
echo "  3. In pubspec.yaml set: repository: https://github.com/corpus-core/colibri-flutter"
echo "  4. dart pub publish from $PKG_DIR"
