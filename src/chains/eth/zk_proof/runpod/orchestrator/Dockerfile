# syntax=docker/dockerfile:1.7
#
# Orchestrator image (runs on the prover server, next to the existing daemon).
#
# It mounts the Colibri period-store docker volume (e.g. `mainnet_blocks`)
# read-write under /data, then in a loop:
#   1. finds the lowest period which has `sync.ssz` but no `zk_proof_g16.bin`,
#   2. uploads `sync.ssz` and the previous period's compressed recursion inputs
#      into a RunPod network volume via the S3-compatible API,
#   3. creates an ephemeral GPU pod (Image 2) which runs the prove,
#   4. polls for the `DONE` marker on the network volume,
#   5. downloads all artifacts, atomically writes them into /data/<period>/,
#   6. terminates the pod and cleans up the job scratch dir.
#
# Build context: repo root (so the Dockerfile can COPY the C sources needed to
# build the verifier CLI as well as the orchestrator sources).
#   docker build \
#     -f src/chains/eth/zk_proof/runpod/orchestrator/Dockerfile \
#     -t <registry>/zkprover-runpod-orchestrator:latest \
#     .

# -----------------------------------------------------------------------------
# Stage 1: build the v5 Groth16 verifier CLI (verify_zk_proof_cli).
#
# The orchestrator verifies every freshly built proof before publishing it,
# against the v5 verification key baked into this branch (the server's own
# verifier is pinned to the v6 program hash and cannot check v5 proofs). Only
# cmake + a C/C++ toolchain are required - no Rust - and we build just this one
# target plus its dependency chain (zk_verifier -> eth_bn254, util, intx_wrapper).
# -----------------------------------------------------------------------------
FROM debian:bookworm-slim AS verifier-builder

RUN apt-get update && apt-get install -y --no-install-recommends \
      cmake \
      clang \
      build-essential \
      git \
      ca-certificates \
      libssl-dev \
      pkg-config \
    && rm -rf /var/lib/apt/lists/*

ENV CC=clang
ENV CXX=clang++
WORKDIR /build

# Only the files cmake needs to configure the project and build the CLI target.
# `bindings/` is required because the CLI targets in src/cli reference
# bindings/colibri_common.c at configure time, even though we only build the
# verifier CLI.
COPY CMakeLists.txt CMakePresets.json ./
COPY src ./src
COPY libs ./libs
COPY scripts ./scripts
COPY bindings ./bindings

# Configure with everything unrelated disabled, then build just the CLI target.
# ETH_ZKPROOF + CLI gate the verify_zk_proof_cli executable; ETH_ZKPROOF_BUILD_HOST
# stays OFF so no Rust/SP1 toolchain is pulled in.
RUN cmake -S . -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DCLI=ON \
      -DETH_ZKPROOF=ON \
      -DPROVER=OFF \
      -DVERIFIER=OFF \
      -DHTTP_SERVER=OFF \
      -DCHAIN_OP=OFF \
      -DEVMONE=OFF \
      -DCURL=OFF \
      -DUSE_MCL=OFF \
      -DETH_ZKPROOF_BUILD_HOST=OFF \
      -DTEST=OFF \
    && cmake --build build --target verify_zk_proof_cli -j "$(nproc)"

# -----------------------------------------------------------------------------
# Stage 2: the Node.js orchestrator runtime.
# -----------------------------------------------------------------------------
FROM node:22-bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY src/chains/eth/zk_proof/runpod/orchestrator/package.json      ./package.json
COPY src/chains/eth/zk_proof/runpod/orchestrator/package-lock.json ./package-lock.json
# `--ignore-scripts` blocks arbitrary lifecycle hooks from any transitive
# dependency, so a compromised @aws-sdk transitive cannot execute code at
# build time. The AWS SDK does not need lifecycle scripts for runtime.
RUN npm ci --omit=dev --ignore-scripts

COPY src/chains/eth/zk_proof/runpod/orchestrator/orchestrator.js ./orchestrator.js

# The v5 Groth16 verifier CLI built in stage 1. orchestrator.js invokes it on
# every freshly built proof before publishing (see VERIFIER_BIN / VERIFY_PROOF).
COPY --from=verifier-builder /build/build/bin/verify_zk_proof_cli /app/verify_zk_proof_cli

# The docker-volume mount point inside the container. Compose maps the host
# volume here (see docker-compose.example.yml).
VOLUME ["/data"]

ENV NODE_ENV=production

CMD ["node", "orchestrator.js"]
