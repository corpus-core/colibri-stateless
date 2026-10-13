# Colibri Stateless -- OSS Scanner build.
#
# This file lives at .oss-scanner/Dockerfile in the colibri-stateless repository.
# The scanner (anthropics/oss-scanner) copies the project checkout into /src, then
# builds this Dockerfile with the checkout as the build context. Network access is
# available only while this file builds; the scan itself runs offline.
#
# One tree is produced, under /src/build/asan:
#
#   clang, RelWithDebInfo (-O1 -g, frame pointers), AddressSanitizer + UndefinedBehavior
#   sanitizer with -fno-sanitize-recover=all. Feature flags mirror the project's
#   `default` CMake preset: CHAIN_ETH + CHAIN_OP, HTTP_SERVER, PROVER_CACHE, EVMONE,
#   ETH_ZKPROOF (verifier side), TEST, CLI. The SP1 host and the Kona-P2P bridge
#   require a Rust toolchain and are intentionally disabled: the frozen SP1 guest
#   ELF is copied beside the binaries instead.
#
# The scanner contract is that failing commands fail the build and the log goes to
# primary_contact. The smoke test at the end therefore prints failures but never
# aborts.

FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive \
    CC=clang-19 \
    CXX=clang++-19

# Build toolchain + CMake FetchContent dependencies (blst, Unity, libuv, llhttp,
# evmone, zstd, tommath, intx, mcl are vendored via FetchContent and need git +
# network, which docker build has). libcurl / openssl headers are needed because
# the server links against libcurl for upstream HTTP calls.
#
# We pin clang-19 (not the stock `clang` = clang-14): evmone makes heavy use of
# C++20 `consteval` functions (see _deps/evmone_external-src/lib/evmone/
# baseline_execution.cpp), and clang-14 rejects them with
#   "call to consteval function '...' is not a constant expression".
# clang-19 is available in Debian 12 main.
#
# libclang-rt-19-dev provides the AddressSanitizer / UndefinedBehaviorSanitizer
# runtime libraries (libclang_rt.asan-*.a, libclang_rt.ubsan_standalone-*.a).
# On Debian 12 these ship separately from `clang-19` and are only pulled in via
# Recommends -- which `--no-install-recommends` strips.
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    git \
    build-essential \
    clang-19 \
    libclang-rt-19-dev \
    cmake \
    ninja-build \
    pkg-config \
    python3 \
    wget \
    libcurl4-openssl-dev \
    libssl-dev \
    protobuf-compiler \
    libprotobuf-dev \
    gdb \
    && update-ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# scanner contract: the checkout lives inside the image, at /src
COPY . /src
WORKDIR /src

# CMake normally derives the project version from `git describe`. Scanner builds get
# the checkout as a tarball-equivalent, so git metadata may be present but a shallow
# clone can lack tags; make sure CMake has a sensible fallback.
#
# Must be >= 3.0.0: the server at src/server/handle_proof.c:499 and the version
# field documented in src/server/server.h:518-534 route any client reporting a
# version < 3.0.0 to a legacy `v2_prover` (which is unconfigured in this image),
# so a sub-3.0 build would reject its own proof requests during the scan.
ENV C4_VERSION=3.0.0-oss-scanner

# Configure the ASan tree.
#
#  CMAKE_BUILD_TYPE=RelWithDebInfo:   keep debug info, let the optimiser surface
#                                      more real bugs than a pure Debug build.
#  -O1 -fno-omit-frame-pointer:        readable sanitizer backtraces.
#  -fsanitize=address,undefined:       the two sanitizers that give the best
#                                      signal-to-noise ratio for this codebase.
#  -fno-sanitize-recover=all:          first UB report aborts, so the agent sees it
#                                      immediately rather than at ctest's end.
#  -D_GNU_SOURCE:                      src/api/colibri_common.c calls memmem() under
#                                      an `#ifdef __GLIBC__` guard, which assumes the
#                                      declaration is visible when glibc is present.
#                                      It is only visible with _GNU_SOURCE (or similar)
#                                      defined; the default preset is a macOS build
#                                      where memmem is POSIX, which hides this on hosts.
#  CHAIN_ETH + CHAIN_OP:               both chain modules compiled in.
#  HTTP_SERVER=ON:                     brings in libuv + llhttp and all handlers.
#  PROVER_CACHE=ON + PROVER_TRACE=ON:  matches the project's default preset so the
#                                      server-side cache paths are exercised.
#  CLI=ON + TEST=ON:                   build the three CLI binaries and unit tests.
#  EVMONE=ON + USE_MCL=OFF:            matches the default preset. With MCL
#                                      off the BN254 operations needed by the
#                                      ZK verifier use the in-tree
#                                      implementation at
#                                      src/chains/eth/bn254/bn254.c.
#  ETH_ZKPROOF=ON:                     builds the ZK verifier (the verifier side,
#                                      which is in scope); the Rust/SP1 **host**
#                                      is skipped because ETH_ZKPROOF_BUILD_HOST
#                                      is left OFF and no Rust toolchain is
#                                      installed, so src/chains/eth/zk_proof/
#                                      falls back to copying the frozen guest ELF.
#  Kona-P2P bridge:                    not linked in; src/chains/op/kona_bridge
#                                      is a standalone CMake subproject that is
#                                      not add_subdirectory()'d from the normal
#                                      build graph, and this image installs no
#                                      Rust toolchain, so it stays out.
#  bindings off (WASM, KOTLIN, SWIFT, C4_PYTHON, DART, NODE_ADDON): out of scope.
RUN cmake -S /src -B /src/build/asan -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER=clang-19 \
    -DCMAKE_CXX_COMPILER=clang++-19 \
    -DCMAKE_C_FLAGS="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all -D_GNU_SOURCE" \
    -DCMAKE_CXX_FLAGS="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all -D_GNU_SOURCE" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCHAIN_ETH=ON \
    -DCHAIN_OP=ON \
    -DHTTP_SERVER=ON \
    -DPROVER_CACHE=ON \
    -DPROVER_TRACE=ON \
    -DCLI=ON \
    -DTEST=ON \
    -DEVMONE=ON \
    -DUSE_MCL=OFF \
    -DETH_ZKPROOF=ON \
    -DETH_ZKPROOF_BUILD_HOST=OFF \
    -DWASM=OFF \
    -DKOTLIN=OFF \
    -DSWIFT=OFF \
    -DC4_PYTHON=OFF \
    -DDART=OFF \
    -DNODE_ADDON=OFF \
    -DEMBEDDED=OFF \
    && cmake --build /src/build/asan -j"$(nproc)"

# Smoke tests. The ASan tree runs them with leak detection off because
# LeakSanitizer needs ptrace, which `docker build` does not grant. Failures are
# printed but do not fail the build (scanner contract). The scan is free to run
# the full suite with ASAN_OPTIONS left unset.
#
# Tests run **serially** (ctest -j1). Several server/libuv tests bind to fixed
# localhost ports (18090 and similar) and will trip over each other under
# parallel execution; stepping on each other's state would mask real crashes.
#
# We skip the server + libuv tests here (`test_server*`, `test_beacon_watcher`,
# `test_http_client_errors`, `test_uv_util`): they spin up a local HTTP server
# on 127.0.0.1 and the libuv event loop interacts with the sandbox in ways
# that are not worth debugging during image build. They still run during the
# scan.
RUN cd /src/build/asan && \
    ASAN_OPTIONS=detect_leaks=0 \
    UBSAN_OPTIONS=print_stacktrace=1 \
    ctest --output-on-failure -j1 --timeout 180 \
      -E '^(test_server|test_beacon_watcher|test_http_client_errors|test_uv_util)' \
    || echo "WARNING: ctest reported failures or timed out"

# Sanity-check that the binaries are usable and link cleanly under ASan.
RUN /src/build/asan/bin/colibri-verifier --version || echo "WARNING: colibri-verifier --version failed"
RUN /src/build/asan/bin/colibri-prover --version    || echo "WARNING: colibri-prover --version failed"
RUN /src/build/asan/bin/colibri-ssz --version       || echo "WARNING: colibri-ssz --version failed"
RUN /src/build/asan/bin/colibri-server --help       || echo "WARNING: colibri-server --help failed"

# Leave the shell at the repository root; everything of interest is under
# /src/build/asan/bin and /src/test/data. See .oss-scanner/threat_model.md for the
# attacker roles, severity rubric, and reproducer examples.
WORKDIR /src
