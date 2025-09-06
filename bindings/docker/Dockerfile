# Use a Debian image for the builder stage
FROM debian:bookworm-slim AS builder

# Install build dependencies including Go
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    clang \
    git \
    build-essential \
    ca-certificates \
    libcurl4-openssl-dev \
    golang-go \
    && update-ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Set working directory for the build
WORKDIR /build

# Copy source code into the builder stage
# We copy CMakeLists.txt first to leverage Docker layer caching for dependencies
COPY CMakeLists.txt ./
COPY src ./src
COPY libs ./libs

# Configure and build the server and OP Stack components
RUN cmake -DHTTP_SERVER=ON -DCMAKE_BUILD_TYPE=Release -DPROOFER_CACHE=true . && make server

# Use a small Debian image for the final stage
FROM debian:bookworm-slim AS final

# Install runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy the built executables from the builder stage
COPY --from=builder /build/bin/server /app/server
COPY --from=builder /build/bin/opg_bridge /app/opg_bridge

# Expose the default server port
EXPOSE 8090

# Default command to run the server
CMD ["/app/server"]
