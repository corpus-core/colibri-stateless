# Colibri Prover Docker Image

This Docker image provides the Colibri Prover server for Ethereum and Layer-2 solutions.

## Quick Start

### Pull from GitHub Container Registry

```bash
# Latest release
docker pull ghcr.io/corpus-core/colibri-prover:latest

# Development version
docker pull ghcr.io/corpus-core/colibri-prover:dev

# Main branch
docker pull ghcr.io/corpus-core/colibri-prover:main

# Specific version
docker pull ghcr.io/corpus-core/colibri-prover:v1.0.0
```

### Run the Server

```bash
docker run -p 8090:8090 ghcr.io/corpus-core/colibri-prover:latest
```

The server will be available at `http://localhost:8090`.

### Docker Compose

```yaml
version: '3.8'
services:
  colibri-prover:
    image: ghcr.io/corpus-core/colibri-prover:latest
    ports:
      - "8090:8090"
    restart: unless-stopped
```

Save this as `docker-compose.yml` and run:

```bash
docker-compose up -d
```

## Building Locally

If you want to build the image yourself:

```bash
docker build -t colibri-prover -f bindings/docker/Dockerfile .
```

## Multi-Architecture Support

Multi-platform images (both architectures) are **only available for releases**:
- `latest` - Latest stable release (linux/amd64, linux/arm64)
- `vX.Y.Z` - Version tags like v1.0.0 (linux/amd64, linux/arm64)

Development and main branch builds use single platform for faster builds:
- `main` - Main branch (linux/amd64 only)
- `dev` - Development branch (linux/amd64 only)

Docker will automatically pull the correct image for your platform. For production use, we recommend using a specific release tag to get multi-architecture support.

## Available Tags

- `latest` - Latest stable release
- `main` - Latest commit on the main branch
- `dev` - Latest commit on the dev branch
- `vX.Y.Z` - Specific version releases (e.g., `v1.0.0`)

## Configuration

The server can be configured via command-line arguments or environment variables. Refer to the [main documentation](https://corpus-core.gitbook.io/specification-colibri-stateless) for details.

## License

**Important:** This Docker image contains the Colibri Prover server, which is licensed under the **PolyForm Noncommercial License 1.0.0**.

- ✅ **Free for non-commercial use**
- ❌ **Commercial use requires a separate license**

For commercial licensing, please contact: [jork@corpus.io](mailto:jork@corpus.io)

See the [license file](../../src/server/LICENSE.POLYFORM) for full details.

## Support

For issues, questions, or contributions:
- GitHub Issues: [github.com/corpus-core/colibri-stateless/issues](https://github.com/corpus-core/colibri-stateless/issues)
- Documentation: [corpus-core.gitbook.io/specification-colibri-stateless](https://corpus-core.gitbook.io/specification-colibri-stateless)

