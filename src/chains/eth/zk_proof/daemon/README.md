# Prover Daemon

This daemon monitors the Beacon Chain finality and automatically triggers the generation of ZK proofs via the SP1 Network.

## Overview

The daemon performs the following steps every 10 minutes:
1. Fetches the `finalized_checkpoint` from the configured Beacon Node RPC.
2. Calculates the current finalized period (`epoch / EPOCHS_PER_PERIOD`).
3. Checks if a proof for the **next** period (`P + 1`) already exists in the output directory.
4. If missing, triggers `scripts/run_zk_proof.sh` to generate the proof using the SP1 Network.
5. Exposes Prometheus metrics (via textfile collector) for monitoring.

## Configuration

The daemon is configured via environment variables.

| Variable | Description | Default |
|----------|-------------|---------|
| `RPC_URL` | Beacon Node RPC URL | `https://lodestar-mainnet.chainsafe.io` |
| `SP1_PRIVATE_KEY` | **Required**. Your SP1 Network Private Key | - |
| `EPOCHS_PER_PERIOD` | Epochs per sync period (256 for Mainnet, 512 for Gnosis) | `256` |
| `CHECK_INTERVAL_MS` | Interval between checks in milliseconds | `600000` (10 mins) |
| `OUTPUT_DIR` | Directory where proofs are stored | `.../build/default/.period_store` |
| `PROMETHEUS_FILE` | Path to write Prometheus metrics | `/metrics/proof.prom` |

## Deployment with Docker

Use the provided `docker-compose.example.yml` as a template.

### 1. Setup Directory
Ensure your host has a directory for the proof data and (optionally) for Prometheus metrics.

```bash
mkdir -p period_store_data
mkdir -p metrics_data
```

### 2. Configure Docker Compose
Create a `docker-compose.yml` (or update your existing stack):

```yaml
version: '3.8'

services:
  prover-daemon:
    build:
      # Important: Context must be the repository root to include scripts/ and src/
      context: ../../../../..
      dockerfile: src/chains/eth/zk_proof/daemon/Dockerfile
    restart: unless-stopped
    environment:
      - RPC_URL=https://lodestar-mainnet.chainsafe.io
      - EPOCHS_PER_PERIOD=256
      - SP1_PRIVATE_KEY=YOUR_KEY_HERE_OR_IN_ENV_FILE
      - OUTPUT_DIR=/data/proofs
      - PROMETHEUS_FILE=/metrics/proof.prom
    volumes:
      - ./period_store_data:/data/proofs
      - ./metrics_data:/metrics
```

### 3. Run
```bash
docker-compose up -d --build
```

## Metrics (Prometheus)

The daemon writes a text file compatible with the Prometheus Node Exporter [Textfile Collector](https://github.com/prometheus/node_exporter#textfile-collector).

**Available Metrics:**
- `prover_daemon_last_check_timestamp_seconds`: Timestamp of the last check loop (Liveness probe).
- `prover_daemon_last_run_timestamp_seconds`: Timestamp of the last *actual* proof generation attempt.
- `prover_daemon_last_run_duration_seconds`: Duration of the last proof generation in seconds.
- `prover_daemon_last_run_status`: Status of the last run (`0` = Success, `1` = Error).
- `prover_daemon_current_period`: The target period currently being processed/checked.

**Alerting Example:**
Trigger an alert if `prover_daemon_last_check_timestamp_seconds` is older than 20 minutes (indicating the daemon is stuck).

