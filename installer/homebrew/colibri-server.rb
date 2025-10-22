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
  
  resource "zstd" do
    url "https://github.com/facebook/zstd/archive/refs/tags/v1.5.6.tar.gz"
    sha256 "30f35f71c1203369dc979ecde0400ffea93c27391bfd2ac5a9715d2173d92ff7"
  end
  
  resource "tommath" do
    url "https://github.com/libtom/libtommath/archive/refs/tags/v1.3.0.tar.gz"
    sha256 "6d099e93ff00fa9b18346f4bcd97dcc48c3e91286f7e16c4ac5515a7171c3149"
  end
  
  resource "evmone" do
    url "https://github.com/ethereum/evmone/archive/refs/tags/v0.15.0.tar.gz"
    sha256 "6eb2122c98bd86a083015b4e41f46b16df4d9bff608d2bf2f2d985ec18e6d640"
  end
  
  resource "intx" do
    url "https://github.com/chfast/intx/archive/refs/tags/v0.10.0.tar.gz"
    sha256 "80513a8ca8b039fa8d40ce88a1910baefc5273259282cf664a10f0707f41cd75"
  end
  
  resource "ethash" do
    url "https://github.com/chfast/ethash/archive/refs/tags/v1.1.0.tar.gz"
    sha256 "73b327f3c23f407389845d936c1138af6328c5841a331c1abe3a2add53c558aa"
  end
  
  def install
    # Extract all resources into their respective directories
    resource("blst").stage { (buildpath/"libs/blst/blst").install Dir["*"] }
    resource("libuv").stage { (buildpath/"libs/libuv/libuv").install Dir["*"] }
    resource("llhttp").stage { (buildpath/"libs/llhttp/llhttp").install Dir["*"] }
    resource("zstd").stage { (buildpath/"libs/zstd/zstd").install Dir["*"] }
    resource("tommath").stage { (buildpath/"libs/tommath/tommath").install Dir["*"] }
    resource("evmone").stage { (buildpath/"libs/evmone/evmone").install Dir["*"] }
    resource("intx").stage { (buildpath/"libs/intx/intx").install Dir["*"] }
    resource("ethash").stage { (buildpath/"libs/evmone/ethash").install Dir["*"] }
    # Build directory
    mkdir "build" do
      system "cmake", "..",
             "-DCMAKE_BUILD_TYPE=Release",
             "-DHTTP_SERVER=ON",
             "-DPROVER=ON",
             "-DPROVER_CACHE=ON",
             "-DVERIFIER=ON",
             "-DCLI=ON",
             "-DTEST=OFF",
             *std_cmake_args
      system "make", "-j#{ENV.make_jobs}", "colibri-server", "colibri-prover", "colibri-verifier", "colibri-ssz"
      
      # Install binaries
      bin.install "bin/colibri-server"
      bin.install "bin/colibri-prover"
      bin.install "bin/colibri-verifier"
      bin.install "bin/colibri-ssz"
    end
    
    # Install config file
    etc.install "installer/config/server.conf.default" => "colibri/server.conf"
    
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

