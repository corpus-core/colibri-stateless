# Build stage
FROM emscripten/emsdk:latest as builder

# Install Node.js and npm
RUN apt-get update && apt-get install -y \
    curl \
    && curl -fsSL https://deb.nodesource.com/setup_18.x | bash - \
    && apt-get install -y nodejs \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source files
COPY . .

# Create build directory and run cmake
RUN rm -rf build && mkdir -p build && cd build && \
    emcmake cmake -DWASM=true -DCLI=false -DTEST=false -DCURL=false -DPROOFER_CACHE=true -DCMAKE_BUILD_TYPE=MINSIZREL .. && \
    make -j$(nproc)

# Runtime stage
FROM node:18-slim

# Create app directory
WORKDIR /app

# Copy only the necessary files from the builder
COPY --from=builder /app/build/emscripten/* ./

# Create cache directory
RUN mkdir -p .cache && \
    echo '{"type": "module"}' > package.json

# Set environment variables
ENV NODE_ENV=production
ENV CACHE_DIR=.cache
ENV CACHE_RAM_LIMIT=100
ENV PORT=8545

# Expose the default port
EXPOSE 8545

# Start the server
CMD ["node", "server.js"] 