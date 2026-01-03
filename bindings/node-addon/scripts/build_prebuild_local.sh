#!/usr/bin/env bash
set -euo pipefail

# Builds a local Node.js native addon (.node) and places artifacts into:
#   bindings/emscripten/prebuilds/<platform>-<arch>/
#
# This is intended as a helper for CI/release automation.
# In release builds you typically want to build the C core as a shared library (libc4)
# and ship it alongside the addon binary. The addon dynamically loads libc4 from the
# same folder as the .node file.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
ADDON_DIR="${ROOT_DIR}/bindings/node-addon"
PKG_PREBUILDS_DIR="${C4W_PREBUILDS_OUT:-${ROOT_DIR}/bindings/emscripten/prebuilds}"

PLATFORM="$(node -p "process.platform")"
ARCH="$(node -p "process.arch")"
OUT_DIR="${PKG_PREBUILDS_DIR}/${PLATFORM}-${ARCH}"

echo "Root: ${ROOT_DIR}"
echo "Addon: ${ADDON_DIR}"
echo "Out:  ${OUT_DIR}"

mkdir -p "${OUT_DIR}"

echo "1) Build libc4 shared library via CMake (SHAREDLIB=1)"
cmake -S "${ROOT_DIR}" -B "${ROOT_DIR}/build/node-addon" -DCMAKE_BUILD_TYPE=Release -DSHAREDLIB=1 -DCURL=0 -DCLI=0 -DUSE_MCL=1
cmake --build "${ROOT_DIR}/build/node-addon" -j

echo "2) Build node addon via node-gyp"
cd "${ADDON_DIR}"
if [[ -f package.json ]]; then
  # Use a local cache to avoid permission issues on shared runners / locked home dirs.
  npm install --no-audit --no-fund --cache "${ADDON_DIR}/.npm-cache"
fi
npx --yes node-gyp configure
npx --yes node-gyp build

echo "3) Copy artifacts"
cp -f "${ADDON_DIR}/build/Release/colibri_native.node" "${OUT_DIR}/colibri_native.node"

# Copy libc4 next to the addon for runtime dlopen().
# Adjust if your build output naming differs.
if [[ "${PLATFORM}" == "darwin" ]]; then
  cp -f "${ROOT_DIR}/build/node-addon/lib/libc4.dylib" "${OUT_DIR}/libc4.dylib"
elif [[ "${PLATFORM}" == "linux" ]]; then
  cp -f "${ROOT_DIR}/build/node-addon/lib/libc4.so" "${OUT_DIR}/libc4.so"
else
  echo "WARNING: libc4 copy step not implemented for platform=${PLATFORM}"
fi

echo "Done."

echo "Prebuild directory content:"
ls -la "${OUT_DIR}" || true


