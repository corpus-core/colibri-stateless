# Homebrew Tap Setup for Colibri Server

This directory contains the Homebrew formula for easy installation of Colibri Server on macOS.

## Important: Formula Location

The `colibri-server.rb` formula should be maintained in a separate GitHub repository for Homebrew distribution:

**Repository:** `corpus-core/homebrew-colibri`  
**URL:** https://github.com/corpus-core/homebrew-colibri

## For Users

📖 **See [HOMEBREW.md](HOMEBREW.md) for complete installation and usage instructions.**

**Quick Start:**
```bash
brew tap corpus-core/colibri
brew install colibri-server
brew services start colibri-server
```

## For Maintainers: Setting Up the Homebrew Tap

### 1. Create the Tap Repository

Create a new GitHub repository named `homebrew-colibri` under the `corpus-core` organization:

```bash
# Repository name MUST start with "homebrew-"
# https://github.com/corpus-core/homebrew-colibri
```

### 2. Add the Formula

```bash
git clone https://github.com/corpus-core/homebrew-colibri.git
cd homebrew-colibri

# Create Formula directory (required by Homebrew)
mkdir -p Formula

# Copy the formula
cp /path/to/colibri-stateless/installer/homebrew/colibri-server.rb Formula/

# Commit and push
git add Formula/colibri-server.rb
git commit -m "Add colibri-server formula"
git push origin main
```

### 3. Update SHA256 for Each Release

When creating a new release, update the SHA256 checksum in the formula:

```bash
# Generate SHA256 for the release tarball
curl -sL https://github.com/corpus-core/colibri-stateless/archive/refs/tags/v1.0.0.tar.gz | shasum -a 256

# Update the formula:
# 1. Edit Formula/colibri-server.rb
# 2. Update the 'url' line with the new version tag
# 3. Update the 'sha256' line with the generated checksum
# 4. Commit and push:
git add Formula/colibri-server.rb
git commit -m "Update colibri-server to v1.0.0"
git push origin main
```

### 4. Testing the Formula Locally

Before pushing changes, test the formula locally:

```bash
# Test installation from local formula
brew install --build-from-source Formula/colibri-server.rb

# Run Homebrew's formula audit
brew audit --strict colibri-server

# Run tests
brew test colibri-server

# Uninstall test installation
brew uninstall colibri-server
```

### 5. Submitting to Homebrew Core (Optional)

Once the formula is stable and popular, you can submit it to Homebrew's core repository:

1. Fork https://github.com/Homebrew/homebrew-core
2. Add your formula to `Formula/c/colibri-server.rb`
3. Submit a pull request

**Requirements for Core:**
- Must have 75+ GitHub stars
- Must be well-documented
- Must pass all audits
- Must have stable versioned releases

## Formula Structure

The `colibri-server.rb` formula:

```ruby
class ColibriServer < Formula
  desc "Trustless stateless-client for Ethereum and L1/L2 networks"
  homepage "https://corpuscore.tech/"
  url "https://github.com/corpus-core/colibri-stateless/archive/refs/tags/v1.0.0.tar.gz"
  sha256 "..."  # Updated for each release
  license "MIT"
  
  depends_on "cmake" => :build
  depends_on "rust" => :build
  depends_on "curl"
  
  def install
    # Build and install binaries
  end
  
  service do
    # Background service configuration
  end
  
  test do
    # Smoke tests
  end
end
```

## Versioning

- Update the `url` line for each new release
- Generate new SHA256 with: `shasum -a 256 <tarball>`
- Follow [Semantic Versioning](https://semver.org/)

## Support

- **GitHub Issues:** https://github.com/corpus-core/colibri-stateless/issues
- **Documentation:** https://corpus-core.gitbook.io/specification-colibri-stateless
- **Email:** jork@corpus.io
