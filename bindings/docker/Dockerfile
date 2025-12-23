# Use a Debian image for the builder stage
FROM debian:bookworm-slim AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    clang \
    git \
    build-essential \
    ca-certificates \
    libcurl4-openssl-dev \
    pkg-config \
    libssl-dev \
    wget \
    && update-ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install Rust (required for Kona-P2P bridge and ZK Proof Host)
RUN wget -O rustup-init.sh https://sh.rustup.rs \
    && chmod +x rustup-init.sh \
    && ./rustup-init.sh -y --default-toolchain stable \
    && rm rustup-init.sh
ENV PATH="/root/.cargo/bin:${PATH}"
ENV CARGO_NET_GIT_FETCH_WITH_CLI=true
ENV CARGO_REGISTRIES_CRATES_IO_PROTOCOL=sparse

# Set working directory for the build
WORKDIR /build

# Copy all source code
COPY CMakeLists.txt ./
COPY CMakePresets.json ./
COPY src ./src
COPY libs ./libs
COPY bindings ./bindings
COPY scripts ./scripts

# Configure and build the server with Kona-P2P bridge
ENV CC=clang
ENV CXX=clang++
RUN cmake -DHTTP_SERVER=ON -DCLI=ON -DCMAKE_BUILD_TYPE=Release -DCHAIN_OP=ON -DCHAIN_ETH=ON -DEVMONE=OFF -DPROVER_CACHE=true -DPROVER_TRACE=true -DETH_ZKPROOF_BUILD_HOST=ON . \
    && make -j"$(nproc)" colibri-server colibri-prover colibri-ssz colibri-verifier cp_signer verify_zk_proof_cli

# Use a small Debian image for the final stage
FROM debian:bookworm-slim AS final

# Install runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy the built server executable from the builder stage
# and the ZK Proof artifacts produced by the CMake target `eth_zkproof_artifacts` and copied next to the server binary.

COPY --from=builder /build/bin/colibri-server /app/colibri-server
COPY --from=builder /build/bin/colibri-prover /app/colibri-prover
COPY --from=builder /build/bin/colibri-ssz /app/colibri-ssz
COPY --from=builder /build/bin/colibri-verifier /app/colibri-verifier
COPY --from=builder /build/bin/cp_signer /app/cp_signer
COPY --from=builder /build/bin/verify_zk_proof_cli /app/verify_zk_proof_cli
COPY --from=builder /build/bin/eth-sync-script /app/eth-sync-script
COPY --from=builder /build/bin/eth_sync_program /app/eth_sync_program

# Allow running binaries directly (e.g. `cp_signer`) without absolute paths.
ENV PATH="/app:${PATH}"

# Add OCI labels for better GHCR integration
LABEL org.opencontainers.image.title="Colibri Prover"
LABEL org.opencontainers.image.description="Highly efficient prover server for Ethereum and Layer-2 solutions"
LABEL org.opencontainers.image.url="https://github.com/corpus-core/colibri-stateless"
LABEL org.opencontainers.image.source="https://github.com/corpus-core/colibri-stateless"
LABEL org.opencontainers.image.documentation="https://corpus-core.gitbook.io/specification-colibri-stateless"
LABEL org.opencontainers.image.vendor="Corpus Core"
LABEL org.opencontainers.image.licenses="PolyForm-Noncommercial-1.0.0"
LABEL org.opencontainers.image.license-notice="This server component is licensed under PolyForm Noncommercial License 1.0.0. Free for non-commercial use only. Commercial use requires a separate license. Contact jork@corpus.io for commercial licensing."

# Expose the default server port
EXPOSE 8090

# Default command to run the server
CMD ["/app/colibri-server"]
