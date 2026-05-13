FROM rust:1.88-slim AS builder

RUN apt-get update && apt-get install -y \
    pkg-config libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Cache dependencies first
COPY Cargo.toml Cargo.lock ./
RUN mkdir src && echo "fn main(){}" > src/main.rs && \
    touch src/lib.rs src/config.rs src/gossip.rs src/http.rs \
          src/processing.rs src/server.rs src/types.rs src/utils.rs && \
    cargo build --release --bin kona-preconf-service 2>/dev/null; true

# Build real binary
COPY src/ src/
RUN cargo build --release --bin kona-preconf-service

# ---- Runtime image ----
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    libssl3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/target/release/kona-preconf-service /usr/local/bin/kona-preconf-service

# Add OCI labels for better GHCR integration
LABEL org.opencontainers.image.title="Colibri Kona Bridge"
LABEL org.opencontainers.image.description="OP Stack preconfirmation P2P service for Colibri"
LABEL org.opencontainers.image.url="https://github.com/corpus-core/colibri-stateless"
LABEL org.opencontainers.image.source="https://github.com/corpus-core/colibri-stateless"
LABEL org.opencontainers.image.vendor="Corpus Core"
LABEL org.opencontainers.image.licenses="PolyForm-Noncommercial-1.0.0"

# P2P ports
EXPOSE 8080
EXPOSE 9090/udp
EXPOSE 9091/tcp

ENV OUTPUT_DIR=/data/preconfs

VOLUME ["/data/preconfs"]

CMD ["kona-preconf-service"]
