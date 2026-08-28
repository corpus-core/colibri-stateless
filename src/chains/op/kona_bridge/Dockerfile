FROM rust:1.91-slim AS builder

RUN apt-get update && apt-get install -y \
    pkg-config libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# Tell openssl-sys to use system OpenSSL (avoids vendored build which needs perl/make)
ENV OPENSSL_NO_VENDOR=1
ENV PKG_CONFIG_ALLOW_CROSS=1

WORKDIR /build

# Cache crate downloads. The dummy binary must not ship: after COPY, touch
# sources so cargo sees them as newer than the dummy artifact (COPY preserves
# host mtimes, which are often older than the RUN that compiled the stub).
COPY Cargo.toml Cargo.lock ./
RUN mkdir src && echo "fn main(){}" > src/main.rs && \
    touch src/lib.rs src/config.rs src/gossip.rs src/http.rs \
    src/processing.rs src/server.rs src/types.rs src/utils.rs && \
    cargo build --release --bin kona-preconf-service; true

COPY src/ src/
RUN find src -name '*.rs' -exec touch {} + \
    && cargo build --release --bin kona-preconf-service \
    && test "$(wc -c < target/release/kona-preconf-service)" -gt 5000000

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
