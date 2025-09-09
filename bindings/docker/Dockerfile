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
    wget \
    && update-ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install Rust (required for Kona-P2P bridge)
RUN wget -O rustup-init.sh https://sh.rustup.rs \
    && chmod +x rustup-init.sh \
    && ./rustup-init.sh -y --default-toolchain stable \
    && rm rustup-init.sh
ENV PATH="/root/.cargo/bin:${PATH}"

# Set working directory for the build
WORKDIR /build

# Copy source code into the builder stage
# We copy CMakeLists.txt first to leverage Docker layer caching for dependencies
COPY CMakeLists.txt ./
COPY CMakePresets.json ./
COPY src ./src
COPY libs ./libs

# Configure and build the server with Kona-P2P bridge
RUN cmake -DHTTP_SERVER=ON -DCMAKE_BUILD_TYPE=Release -DCHAIN_OP=ON -DPROOFER_CACHE=true . && make server

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
COPY --from=builder /build/bin/server /app/server

# Expose the default server port
EXPOSE 8090

# Default command to run the server
CMD ["/app/server"]
