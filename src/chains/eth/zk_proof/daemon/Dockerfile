FROM node:20-bullseye

# Install system dependencies
RUN apt-get update && apt-get install -y \
    curl \
    build-essential \
    pkg-config \
    libssl-dev \
    git \
    clang \
    cmake \
    && rm -rf /var/lib/apt/lists/*

# Install Rust
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
ENV PATH="/root/.cargo/bin:${PATH}"

# Install SP1 Toolchain (sp1up)
# CRITICAL: We pin the toolchain version to match the one used in run_zk_proof.sh (PkFc33VNGO)
# This ensures the Verification Key (VK) is consistent with local builds.
RUN curl -L https://sp1.succinct.xyz | bash
ENV PATH="/root/.sp1/bin:${PATH}"

# Pre-install SP1 toolchain
# Note: run_zk_proof.sh logic will try to use "PkFc33VNGO" or fallback to latest.
# Ideally, we should install the specific pinned version if sp1up supports it, or rely on the script to install it.
# But running sp1up installs the latest, which is a good baseline.
RUN sp1up

# Set working directory
WORKDIR /app

# Copy only necessary directories to keep image small and build context clean
# We need scripts/ for the runner and src/chains/eth/zk_proof for the program code
# We also need src/util and src/chains/eth/ssz/zk_verifier/bn254 etc for the C-Verifier build.
# To be safe and simple for the C build (which has many relative includes), we copy src/ and libs/
COPY scripts/ ./scripts/
COPY src/ ./src/
COPY libs/ ./libs/
COPY CMakeLists.txt ./CMakeLists.txt
COPY CMakePresets.json ./CMakePresets.json

# Build C-Verifier
# We configure only the verifier target to save time
# We explicitly set the output directory to build/default to match the daemon's expectation (and standard cmake behavior)
# but standard cmake installs to bin/. Let's check where it lands.
# Assuming standard cmake layout: build/default/bin/verify_zk_proof_cli or similar.
RUN cmake -S . -B build/default -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build/default --target verify_zk_proof_cli

# Install Daemon dependencies
WORKDIR /app/src/chains/eth/zk_proof/daemon
RUN npm install

# Set back to root for runtime context if needed, but daemon script uses absolute paths or resolves relative to __dirname
# However, for clarity, let's stay in daemon dir or root. 
# The script spawns run_zk_proof.sh with CWD = REPO_ROOT, so it doesn't matter much where we launch node from as long as paths resolve.
WORKDIR /app/src/chains/eth/zk_proof/daemon

# Start the daemon
CMD ["node", "index.js"]

