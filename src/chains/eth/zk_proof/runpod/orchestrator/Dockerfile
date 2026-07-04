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
# Build context: repo root (so the Dockerfile can COPY sibling files if needed).
#   docker build \
#     -f src/chains/eth/zk_proof/runpod/orchestrator/Dockerfile \
#     -t <registry>/zkprover-runpod-orchestrator:latest \
#     .
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

# The docker-volume mount point inside the container. Compose maps the host
# volume here (see docker-compose.example.yml).
VOLUME ["/data"]

ENV NODE_ENV=production

CMD ["node", "orchestrator.js"]
