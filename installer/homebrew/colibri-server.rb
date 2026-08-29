# Homebrew Formula for Colibri Server
# This file should be placed in a GitHub repository: corpus-core/homebrew-colibri
# Users can then install with: brew tap corpus-core/colibri && brew install colibri-server

class ColibriServer < Formula
  desc "Trustless stateless-client for Ethereum and L1/L2 networks"
  homepage "https://corpuscore.tech/"
  url "https://github.com/corpus-core/colibri-stateless/archive/refs/tags/v1.0.0.tar.gz"
  sha256 "REPLACE_WITH_ACTUAL_SHA256"  # Generate with: shasum -a 256 <tarball>
  license "MIT"
  
  head "https://github.com/corpus-core/colibri-stateless.git", branch: "main"
  
  depends_on "cmake" => :build
  depends_on "rust" => :build
  depends_on "curl"
  
  # External dependencies (normally fetched via FetchContent, but Homebrew doesn't allow that)
  resource "blst" do
    url "https://github.com/supranational/blst/archive/refs/tags/v0.3.13.tar.gz"
    sha256 "89772cef338e93bc0348ae531462752906e8fa34738e38035308a7931dd2948f"
  end
  
  resource "libuv" do
    url "https://github.com/libuv/libuv/archive/refs/tags/v1.50.0.tar.gz"
    sha256 "b1ec56444ee3f1e10c8bd3eed16ba47016ed0b94fe42137435aaf2e0bd574579"
  end
  
  resource "llhttp" do
    url "https://github.com/nodejs/llhttp/archive/refs/tags/release/v9.2.1.tar.gz"
    sha256 "3c163891446e529604b590f9ad097b2e98b5ef7e4d3ddcf1cf98b62ca668f23e"
  end
  
  # Required for CHAIN_OP (OP-Stack preconf ZSTD decompression).
  resource "zstd" do
    url "https://github.com/facebook/zstd/archive/refs/tags/v1.5.6.tar.gz"
    sha256 "30f35f71c1203369dc979ecde0400ffea93c27391bfd2ac5a9715d2173d92ff7"
  end
  
  resource "tommath" do
    url "https://github.com/libtom/libtommath/archive/refs/tags/v1.3.0.tar.gz"
    sha256 "6d099e93ff00fa9b18346f4bcd97dcc48c3e91286f7e16c4ac5515a7171c3149"
  end
  
  resource "evmone" do
    url "https://github.com/ipsilon/evmone/archive/refs/tags/v0.19.0.tar.gz"
    sha256 "ee9b5f5375410bebf67a092b495c8009aa1484dfdb950d647b769f99df940e58"
  end
  
  resource "intx" do
    url "https://github.com/chfast/intx/archive/refs/tags/v0.15.0.tar.gz"
    sha256 "7db5d37ae5e9c3787a12c27e53a28be840a35ee51101c3ac15412ce259191600"
  end

  resource "evmc" do
    url "https://github.com/ethereum/evmc/archive/refs/tags/v12.1.0.tar.gz"
    sha256 "0d5458015bf38a5358fad04cc290d21ec40122d1eb6420e0b33ae25546984bcd"
  end
  
  def install
    # Extract all resources into CMake's expected _deps structure
    resource("blst").stage { (buildpath/"build/_deps/blst-src").install Dir["*"] }
    resource("libuv").stage { (buildpath/"build/_deps/libuv-src").install Dir["*"] }
    resource("llhttp").stage { (buildpath/"build/_deps/llhttp-src").install Dir["*"] }
    resource("zstd").stage { (buildpath/"build/_deps/zstd-src").install Dir["*"] }
    resource("tommath").stage { (buildpath/"build/_deps/libtommath-src").install Dir["*"] }
    resource("evmone").stage { (buildpath/"build/_deps/evmone_external-src").install Dir["*"] }
    resource("intx").stage { (buildpath/"build/_deps/intx-src").install Dir["*"] }
    # evmc is a submodule of evmone
    resource("evmc").stage { (buildpath/"build/_deps/evmone_external-src/evmc").install Dir["*"] }
    
    # Build directory
    mkdir "build" do
      # Tell CMake where to find the pre-extracted dependencies (bypasses FetchContent)
      system "cmake", "..",
             "-DCMAKE_BUILD_TYPE=Release",
             "-DHTTP_SERVER=ON",
             "-DCHAIN_OP=ON",
             "-DPROVER=ON",
             "-DPROVER_CACHE=ON",
             "-DVERIFIER=ON",
             "-DCLI=ON",
             "-DETH_ZKPROOF_BUILD_HOST=OFF",
             "-DTEST=OFF",
             "-DFETCHCONTENT_FULLY_DISCONNECTED=ON",
             "-DFETCHCONTENT_SOURCE_DIR_BLST=#{buildpath}/build/_deps/blst-src",
             "-DFETCHCONTENT_SOURCE_DIR_LIBUV=#{buildpath}/build/_deps/libuv-src",
             "-DFETCHCONTENT_SOURCE_DIR_LLHTTP=#{buildpath}/build/_deps/llhttp-src",
             "-DFETCHCONTENT_SOURCE_DIR_ZSTD=#{buildpath}/build/_deps/zstd-src",
             "-DFETCHCONTENT_SOURCE_DIR_LIBTOMMATH=#{buildpath}/build/_deps/libtommath-src",
             "-DFETCHCONTENT_SOURCE_DIR_EVMONE_EXTERNAL=#{buildpath}/build/_deps/evmone_external-src",
             "-DFETCHCONTENT_SOURCE_DIR_INTX=#{buildpath}/build/_deps/intx-src",
             *std_cmake_args
      system "make", "-j#{ENV.make_jobs}", "colibri-server", "colibri-prover", "colibri-verifier", "colibri-ssz"
      
      # Install binaries
      bin.install "bin/colibri-server"
      bin.install "bin/colibri-prover"
      bin.install "bin/colibri-verifier"
      bin.install "bin/colibri-ssz"
    end
    
    # Install config file (path relative to buildpath, not build dir)
    (etc/"colibri").install buildpath/"installer/config/server.conf.default" => "server.conf"
    
    # Zsh completions
    zsh_completion.install "scripts/completion/_colibri"
    zsh_completion.install "scripts/completion/_colibri-prover"
    zsh_completion.install "scripts/completion/_colibri-ssz"
    zsh_completion.install "scripts/completion/_colibri-server"
    
    # Documentation
    doc.install "README.md"
  end
  
  service do
    run [opt_bin/"colibri-server", "-f", etc/"colibri/server.conf"]
    keep_alive true
    log_path var/"log/colibri-server.log"
    error_log_path var/"log/colibri-server.error.log"
    environment_variables PATH: std_service_path_env
  end
  
  test do
    # Test that the binaries run and show help
    system "#{bin}/colibri-server", "--help"
    system "#{bin}/colibri-prover", "--help"
    system "#{bin}/colibri-verifier", "--help"
    system "#{bin}/colibri-ssz", "--help"
  end
end

