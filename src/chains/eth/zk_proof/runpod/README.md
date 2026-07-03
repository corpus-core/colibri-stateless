# RunPod ZK-Proof Pipeline

Automates the daily v5 Groth16 sync-committee proof by offloading the actual
prove step to ephemeral GPU pods on [RunPod](https://runpod.io), while the
existing Colibri prover server keeps its role as the persistent data store.

This replaces the manual `scripts/build_proof` workflow (~40 min on an
Apple-Silicon MacBook, purely CPU) with a fully automated pipeline. The
Frozen-Guest-ELF, the Groth16 v5.0.0 circuit and the recursion chain are
untouched, so the on-chain verification key and the `zk_sync_keys_root`
trust anchor stay stable and every existing verifier keeps working.

## Contents

- [`orchestrator/`](orchestrator/) - **Image 1**, runs on the prover server,
  scans the period-store docker volume, hands off missing proofs to RunPod
  and writes results back atomically.
- [`prover-gpu/`](prover-gpu/) - **Image 2**, ephemeral GPU pod image. Runs
  `moongate-server` natively (no docker-in-docker) and executes the SP1
  `--prove --groth16` step for a single period.

## Architecture

```
 ┌───────────────────────────────┐         ┌─────────────────────────────┐
 │ Prover server                 │         │ RunPod                      │
 │                               │         │                             │
 │ ┌───────────────────────────┐ │  REST   │  ┌──────────────────────┐   │
 │ │ zkprover-runpod-          │◄┼─────────┼─►│ /v1/pods             │   │
 │ │   orchestrator (Image 1)  │ │         │  └──────────────────────┘   │
 │ │                           │ │  S3     │  ┌──────────────────────┐   │
 │ │  scan / upload / download │◄┼─────────┼─►│ Network Volume       │   │
 │ │                           │ │         │  │   /workspace         │   │
 │ └───────────────┬───────────┘ │         │  └───────────┬──────────┘   │
 │                 │             │         │              │              │
 │  ┌──────────────▼─────────┐   │         │  ┌───────────▼──────────┐   │
 │  │ docker volume          │   │         │  │ Ephemeral GPU pod    │   │
 │  │   /data/<period>/      │   │         │  │  zkprover-gpu        │   │
 │  │     sync.ssz           │   │         │  │   (Image 2)          │   │
 │  │     zk_proof_g16.bin   │   │         │  │  moongate-server +   │   │
 │  │     ...                │   │         │  │  eth-sync-script     │   │
 │  └────────────────────────┘   │         │  └──────────────────────┘   │
 └───────────────────────────────┘         └─────────────────────────────┘
```

## Data flow

For each period `P` the orchestrator finds a `sync.ssz` for but no
`zk_proof_g16.bin`:

1. Verify the previous period `P-1` has the recursion inputs (`zk_proof.bin`,
   `zk_vk_raw.bin`). If not, log a warning and skip - the chain must be built
   period-by-period.
2. Upload `sync.ssz` and the previous period's recursion inputs to the RunPod
   network volume under `jobs/<chain>/<P>/in/` using the S3-compatible API.
3. Create a fresh GPU pod via the RunPod REST API. The pod mounts the same
   network volume at `/workspace` and receives `CHAIN` + `PERIOD` in its env.
4. The pod's `entrypoint.sh`:
   - lays out `/workspace/jobs/<chain>/<P>/store/{<P>/sync.ssz,<P-1>/...}`;
   - starts `moongate-server` natively on `127.0.0.1:3000`;
   - runs `run_zk_proof.sh --prove --groth16 --period <P> --output store`,
     with `SP1_PROVER=cuda` and `SP1_MOONGATE_ENDPOINT=http://127.0.0.1:3000`
     so `main.rs` builds the SDK client via
     `ProverClient::builder().cuda().server(...)` (docker-free);
   - copies the six artifacts into `.../out/` and writes an empty `DONE`
     marker; on any error it writes `FAILED` and exits non-zero.
5. The orchestrator polls the network volume for `DONE`/`FAILED` and the pod
   status. On `DONE` it downloads the artifacts, writes them atomically into
   `/data/<P>/` (matching what `scripts/build_proof` uploads today), then
   terminates the pod and removes the job scratch dir from the network
   volume.

Artifacts written back to the server volume (same set as
`scripts/build_proof`'s `UPLOAD_FILES`):

- `zk_proof_g16.bin` - final Groth16 proof (required by verifier)
- `zk_pub.bin` - public inputs (required)
- `zk_proof.bin` - compressed SP1 proof (recursion input for `P+1`)
- `zk_vk_raw.bin` - raw verifying key bytes (recursion input for `P+1`)
- `zk_groth16.bin` - Groth16 proof wrapper
- `zk_vk.bin` - hashed VK

## Setup

### One-time on the RunPod side

1. **Network volume**. Create a network volume in the datacenter you plan to
   run GPU pods in (Console -> Storage -> Network Volumes). Size 100 GB is
   sufficient (proof scratch + 4 GB Groth16 circuit cache). Note the volume
   id and the datacenter (e.g. `EU-RO-1`).
2. **S3 API credentials**. Create an S3-compatible access key/secret pair for
   the volume (Console -> Storage -> S3 API). Store both keys as Docker
   secrets on the prover server.
3. **REST API key**. Console -> Settings -> API Keys. Give it "Read/Write"
   scope. Also store as a Docker secret.
4. **Circuit pre-population (optional)**. The prover image expects the
   Groth16 v5.0.0 circuit under `/workspace/.sp1/circuits/groth16/v5.0.0/`.
   The first pod that runs will download it once (~4 GB) and every
   subsequent pod on the same volume reuses it. If you want to avoid that
   cold start, upload `.sp1/circuits/groth16/v5.0.0/*` from your workstation
   to `s3://<VOLUME_ID>/.sp1/circuits/groth16/v5.0.0/` before running the
   orchestrator for the first time.

### Building and pushing the prover image

The prover image bundles the SP1 host binary (built with `--features cuda`),
the frozen guest ELF, `run_zk_proof.sh`, `moongate-server` (extracted from
`public.ecr.aws/succinct-labs/moongate`) and the entrypoint. The build takes
around 20-30 min on a native x86_64 runner because of the Rust release build
and the moongate-server extract; on an Apple-Silicon MacBook it takes hours
under QEMU emulation, so the CI workflow below is the recommended path.

#### Option A - GitHub Actions (recommended)

This is a temporary, branch-local pipeline. The v5 pod solution lives only on
the `v5_pod` branch and is intentionally **not** merged into `dev`/`main` (the
main branches already build v6 proofs via the SP1 prover network). Because of
that, the workflow is driven purely by pushes to `v5_pod`:

- **File**: [`.github/workflows/prover-gpu-docker.yml`](../../../../../.github/workflows/prover-gpu-docker.yml)
- **Trigger**: `push` to the `v5_pod` branch. A `push` trigger uses the
  workflow file of the pushed branch, so it does **not** need to exist on the
  default branch (`dev`). This is deliberate - `workflow_dispatch` /
  `gh workflow run` would require the file on `dev`, which we avoid.
- **Authentication**: uses the built-in `GITHUB_TOKEN` with `packages: write`,
  so no PAT or secret setup is required.
- **How to (re)deploy**: push to `v5_pod`. To force a rebuild without source
  changes, push an empty commit:

  ```bash
  git commit --allow-empty -m "rebuild prover-gpu image"
  git push origin v5_pod
  ```

  The image is published as
  `ghcr.io/corpus-core/colibri-prover-gpu:latest` (plus `v5_pod` and a
  short-SHA tag) and appears under
  [https://github.com/orgs/corpus-core/packages](https://github.com/orgs/corpus-core/packages)
  as `colibri-prover-gpu`.

- **Visibility**: newly-created GHCR packages default to *private*. RunPod
  pulls images anonymously by default, so after the first successful push
  open the package page, go to *Package settings -> Danger zone -> Change
  visibility* and pick **Public**. Alternatively keep the package private
  and register a container-registry credential in the RunPod console
  (Console -> Settings -> Container Registries) before the orchestrator
  creates its first pod.

#### Option B - Local `docker buildx` (fallback)

Only convenient on a native amd64 host (Linux server, x86_64 build VM). On
Apple-Silicon this cross-compiles the Rust host binary under QEMU and is
significantly slower than the CI job. Login with a personal access token
that has the `write:packages` scope:

```bash
echo "$GHCR_PAT" | docker login ghcr.io -u <your-gh-username> --password-stdin

docker buildx build \
  --platform linux/amd64 \
  -f src/chains/eth/zk_proof/runpod/prover-gpu/Dockerfile \
  -t ghcr.io/corpus-core/colibri-prover-gpu:latest \
  --push \
  .
```

The `--features cuda` build only enables the SDK's CUDA client; it does not
require CUDA to be installed at *build* time. The build stage uses a plain
`rust:1.81-slim` base.

#### Version stability

The image tracks `:latest` off the `v5_pod` branch, so there is no version tag
to bump. The pieces that must stay frozen for the v5 proofs to keep verifying
are the `sp1-sdk` version (`Cargo.lock` in `src/chains/eth/zk_proof/`), the
matching `MOONGATE_IMAGE` build arg in
`src/chains/eth/zk_proof/runpod/prover-gpu/Dockerfile`, the Frozen-Guest-ELF
and the Groth16 v5.0.0 circuit. Keep all four unchanged so the on-chain VK and
the `zk_sync_keys_root` trust anchor stay stable.

### Running the orchestrator

Copy `orchestrator/docker-compose.example.yml` to
`orchestrator/docker-compose.yml`, fill in the credentials and the
`RUNPOD_IMAGE` you pushed, then:

```bash
cd src/chains/eth/zk_proof/runpod/orchestrator
docker compose up -d --build
docker compose logs -f zkproof-runpod-orchestrator
```

The orchestrator holds no state of its own: everything it needs lives on the
mounted period-store docker volume and on the RunPod network volume. It is
safe to restart at any time.

## Environment variables

Only variables used by the orchestrator. The prover-gpu image is driven
entirely via the pod-create call (`CHAIN` + `PERIOD`).

| Var | Required | Default | Purpose |
|-----|----------|---------|---------|
| `CHAIN` | yes | - | Chain label; used to namespace paths on the network volume. Must match the mounted docker volume. |
| `RUNPOD_API_KEY` | yes | - | RunPod REST API key. |
| `RUNPOD_S3_ACCESS_KEY` | yes | - | RunPod S3-compatible access key for the network volume. |
| `RUNPOD_S3_SECRET_KEY` | yes | - | RunPod S3-compatible secret key. |
| `NETWORK_VOLUME_ID` | yes | - | RunPod network volume id (used as the S3 bucket name). |
| `RUNPOD_DATACENTER` | yes | - | Datacenter of the network volume, e.g. `EU-RO-1`. Determines the S3 endpoint. |
| `RUNPOD_IMAGE` | yes | - | Docker image for the ephemeral GPU pod. |
| `RUNPOD_GPU_TYPES` | no | `NVIDIA GeForce RTX 4090` | Comma-separated preference list of GPU types. |
| `OUTPUT_DIR` | no | `/data` | Mount point of the docker volume inside the orchestrator container. |
| `CHECK_INTERVAL_MS` | no | `600000` | Interval between volume scans (10 min default). |
| `JOB_TIMEOUT_MS` | no | `3600000` | Max time to wait for one pod to publish `DONE` (60 min default). |
| `POLL_INTERVAL_MS` | no | `15000` | How often to check pod status + S3 markers while a job runs. |
| `CONTAINER_DISK_GB` | no | `20` | Pod container-disk size (GB). |

For every required variable you can alternatively set `<NAME>_FILE` (for
example `RUNPOD_API_KEY_FILE=/run/secrets/runpod_api_key`). The orchestrator
reads the file contents at startup and never exposes the value via
`process.env`. This is the recommended pattern for Docker Compose or Docker
Swarm secrets, and it matches how `scripts/run_zk_proof.sh` already handles
the same credentials on the laptop workflow.

## Operations

- **Logs**: `docker compose logs -f zkproof-runpod-orchestrator` on the
  prover server. Every action (upload, pod create, poll, download,
  terminate) is timestamped.
- **Manual re-run**: delete the produced `zk_proof_g16.bin` under
  `/data/<P>/`. The next tick will pick that period up again.
- **Backfill many periods**: the loop processes periods bottom-up until it
  runs out of missing ones. Bring your volume online with the missing
  periods present and the orchestrator will process them one after another.
- **Cost control**: an ephemeral pod is created only when there is actual
  work. On idle ticks nothing is provisioned. `JOB_TIMEOUT_MS` guards against
  runaway pods (they are terminated regardless of the outcome). If a job
  fails, the orchestrator backs off to the next tick interval instead of
  retrying immediately, so a persistent error (e.g. bad image tag or expired
  credentials) cannot burn a series of GPU-hours before an operator notices.
- **Graceful shutdown**: on `SIGTERM`/`SIGINT` (e.g. `docker compose down`,
  restart on OOM) the orchestrator terminates the active RunPod pod before
  exiting so credits are not billed for an orphaned job.
- **Verification (optional enhancement)**: the daemon in
  [`src/chains/eth/zk_proof/daemon`](../daemon) verifies newly-created proofs
  with `verify_zk_proof_cli` before they are marked as "final" for clients.
  This orchestrator relies on the same downstream verification pass; do not
  disable it.

## Notes and known constraints

- **Recursion chain**. The prev-period recursion inputs are mandatory. The
  orchestrator refuses to prove a period whose predecessor is not yet
  present. If a proof was skipped for an earlier period, run
  `scripts/build_proof` (or trigger this orchestrator for that period) first
  to restore the chain.
- **`SP1_SKIP_VERIFY=1`** is set inside the GPU pod because container images
  do not ship a separate verifier. The orchestrator downloads the proof and
  the existing server-side verifier chain (Colibri C verifier) confirms
  correctness end-to-end.
- **`moongate-server` tag**. The `MOONGATE_IMAGE` build arg defaults to
  `public.ecr.aws/succinct-labs/moongate:v5.0.0`. Verify at build time that
  this matches the `sp1-sdk` version (`5.2.3` in this workspace). If Succinct
  ever bumps the moongate image and the wire protocol, pin an appropriate
  tag with `--build-arg MOONGATE_IMAGE=...`.
