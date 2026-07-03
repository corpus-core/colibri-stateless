# syntax=docker/dockerfile:1.7
#
# GPU prover image for RunPod (ephemeral pods).
#
# Runs the SP1 host binary against a natively-hosted moongate-server so no
# docker-in-docker is needed at proving time. Inputs (`sync.ssz` and the
# previous period's compressed recursion proof) come from a RunPod network
# volume mounted at `/workspace`; outputs (`zk_proof_g16.bin`, `zk_pub.bin`,
# `zk_proof.bin`, `zk_vk_raw.bin`, `zk_groth16.bin`, `zk_vk.bin`) are written
# back to the same volume together with a `DONE` (or `FAILED`) marker so the
# orchestrator can pick them up.
#
# Build context: repository root (same as the existing daemon image), for
# example:
#   docker build \
#     -f src/chains/eth/zk_proof/runpod/prover-gpu/Dockerfile \
#     -t <registry>/zkprover-gpu:v5.2.3 \
#     .
#
# Build args:
#   MOONGATE_IMAGE - the succinct-labs image to extract `moongate-server` from.
#                    Must match the sp1-sdk version compiled into the host
#                    binary (5.2.3 in this workspace).
#   CUDA_RUNTIME   - CUDA 12 runtime base image for the final stage.
#   RUST_VERSION   - toolchain for the host build stage. Must be >= 1.85 because
#                    the alloy dependency tree pulled in by sp1-sdk 5.2.3
#                    requires the stabilized `edition2024` Cargo feature
#                    (stabilized in Rust 1.85). Pinned to the same toolchain the
#                    laptop workflow builds with (1.91) to avoid MSRV surprises.
ARG MOONGATE_IMAGE=public.ecr.aws/succinct-labs/moongate:v5.0.0
ARG CUDA_RUNTIME=nvidia/cuda:12.2.2-runtime-ubuntu22.04
ARG RUST_VERSION=1.91

# ---- 1. Extract moongate-server from the official Succinct image ------------
# This gives us a native `moongate-server` binary we can run directly next to
# the host process (instead of the SDK spawning a Docker container).
FROM ${MOONGATE_IMAGE} AS moongate

# ---- 2. Build the SP1 host binary with the `cuda` feature -------------------
FROM rust:${RUST_VERSION}-slim-bookworm AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
      pkg-config \
      libssl-dev \
      ca-certificates \
      git \
      build-essential \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Only copy the Rust workspace and its lockfile (see .dockerignore exception).
COPY src/chains/eth/zk_proof/Cargo.toml /src/zk_proof/Cargo.toml
COPY src/chains/eth/zk_proof/Cargo.lock /src/zk_proof/Cargo.lock
COPY src/chains/eth/zk_proof/common /src/zk_proof/common
COPY src/chains/eth/zk_proof/program /src/zk_proof/program
COPY src/chains/eth/zk_proof/script /src/zk_proof/script
COPY src/chains/eth/zk_proof/export_vk /src/zk_proof/export_vk

# The host binary talks to moongate via HTTP; the `cuda` feature only pulls in
# the client-side pieces, so no CUDA linker deps are required at build time.
# zk_proof is a cargo workspace, so the build output lands in the workspace
# target dir (/src/zk_proof/target), not under the script member.
RUN cd /src/zk_proof/script \
    && cargo build --release --features cuda --bin eth-sync-script \
    && { strip /src/zk_proof/target/release/eth-sync-script || true; }

# ---- 3. Runtime stage: CUDA runtime + moongate + host binary ---------------
FROM ${CUDA_RUNTIME}

# Small runtime footprint: coreutils for `cp`/`mkdir`, ca-certs for HTTPS
# (needed by sp1-sdk when fetching circuit artifacts on first run).
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates \
      coreutils \
      bash \
    && rm -rf /var/lib/apt/lists/*

# moongate-server (native binary extracted from succinctlabs/moongate:v5.0.0).
# The upstream image installs the binary at /usr/local/bin/moongate-server
# (see succinctlabs/sp1-gpu Dockerfile.server). If Succinct ever relocates it,
# override MOONGATE_IMAGE and adjust the source path here.
COPY --from=moongate /usr/local/bin/moongate-server /usr/local/bin/moongate-server
RUN chmod +x /usr/local/bin/moongate-server

# Host binary + frozen guest ELF. run_zk_proof.sh detects this /app layout
# (see its `if [ -f "/app/eth_sync_program" ] && ...` branch) and skips both
# the toolchain check and the host rebuild - the image is fully self-contained.
COPY --from=builder /src/zk_proof/target/release/eth-sync-script /app/eth-sync-script
COPY src/chains/eth/zk_proof/program/elf/eth_sync_program /app/eth_sync_program
COPY scripts/run_zk_proof.sh /app/scripts/run_zk_proof.sh
COPY src/chains/eth/zk_proof/runpod/prover-gpu/entrypoint.sh /app/entrypoint.sh

RUN chmod +x /app/eth-sync-script /app/entrypoint.sh /app/scripts/run_zk_proof.sh

ENV PATH="/app:${PATH}" \
    MOONGATE_PORT=3000 \
    # The Groth16 circuit cache is shared across pods via the network volume.
    # The entrypoint symlinks $HOME/.sp1/circuits -> /workspace/.sp1/circuits
    # so the first pod downloads once (~4 GB) and every subsequent pod reuses
    # it. Users can also pre-populate it via the S3 API from the orchestrator.
    HOME=/root

WORKDIR /app

ENTRYPOINT ["/app/entrypoint.sh"]
