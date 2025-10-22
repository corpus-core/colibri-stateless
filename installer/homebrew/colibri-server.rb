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
  
  def install
    # Build directory
    mkdir "build" do
      system "cmake", "..",
             "-DCMAKE_BUILD_TYPE=Release",
             "-DHTTP_SERVER=ON",
             "-DPROVER=ON",
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

