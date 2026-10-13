# Colibri Stateless — Threat model and severity guide for the scanner

This file is read by the OSS Scanner before it starts. It describes what Colibri
Stateless is, where untrusted input enters, which components matter, how to
exercise them inside the scanner image, and how we would like findings to be
rated and reported.

For protocol-level considerations (sync-committee compromise, weak subjectivity,
reorgs) see [`src/chains/eth/threat_model.md`](../src/chains/eth/threat_model.md).
That file documents the design assumptions and is not an implementation review.
This file steers the scan toward implementation bugs — memory safety, parser
correctness, cryptographic verification logic, and server input handling.

Project docs worth reading first (they are in the image):

- [`README.md`](../README.md) — product overview.
- [`AGENTS.md`](../AGENTS.md), [`src/AGENTS.md`](../src/AGENTS.md),
  [`src/util/AGENTS.md`](../src/util/AGENTS.md),
  [`src/chains/eth/AGENTS.md`](../src/chains/eth/AGENTS.md),
  [`src/server/AGENTS.md`](../src/server/AGENTS.md),
  [`src/cli/AGENTS.md`](../src/cli/AGENTS.md) — architecture per module.
- [`src/chains/eth/threat_model.md`](../src/chains/eth/threat_model.md) —
  protocol-level threat model.
- [`src/server/API.md`](../src/server/API.md),
  [`src/server/openapi.yaml`](../src/server/openapi.yaml) — public HTTP API.

## What Colibri Stateless is and where untrusted input enters

Colibri Stateless is a prover / verifier for Ethereum (and OP-Stack L2) RPC
data. It is written in portable C and shipped as a library, a set of CLI tools,
and an optional HTTP server. The core guarantee is that a **verifier** running
on an end-user device (wallet, embedded gadget, browser, mobile app) can
cryptographically check an RPC response against a BLS-signed beacon-chain state
without running a full node. The **prover** collects raw data from untrusted
RPC and Beacon-API nodes and produces an SSZ-encoded proof. The verifier only
trusts the aggregated signature of the Ethereum sync committee (bootstrapped
from a checkpoint or a recursive ZK proof).

### Trust boundaries

The two interesting attacker roles are:

1. **Malicious prover (or malicious intermediate network).** Supplies an
   attacker-controlled SSZ proof blob (and, in some sync paths, light-client
   updates and ZK proofs) to a verifier that runs inside a user application.
   The verifier must either accept a correct proof and compute the right result
   or reject the proof. Any other outcome — crash, memory corruption, wrong
   result, infinite loop, large allocation — is a finding.

2. **Malicious upstream node (RPC or Beacon API).** Returns attacker-controlled
   JSON / SSZ to a prover. The prover is typically a server operator's trusted
   process, but it must still handle malformed or hostile responses safely
   (memory safety, no persistent corruption of the server, no bypass of the
   downstream verifier). The same attacker role applies when the **verifier
   runs with `-m local` or `-m hybrid`** (see "How to exercise it" below):
   the prover code then runs inside the user's own application and the
   malicious RPC / Beacon-API bytes reach it directly.

A separate role is a **malicious HTTP client** of the optional prover server
(`colibri-server`, built from [`src/server/`](../src/server/)). The server
exposes POST endpoints (`/proof`, `/rpc`, `/unverified_rpc`) and a number of
GET endpoints that take attacker-controlled URL parameters (see "Where
attacker-controlled bytes enter" below). Local operator configuration
(`c4_config.json`, environment variables, CLI flags) is **trusted** — a bug
only reachable by setting an invalid flag or writing a bad config is not a
finding on its own.

A separate attacker role applies to **`colibri-signer`** (the checkpoint
signer, built from
[`src/chains/eth/cp_signer/`](../src/chains/eth/cp_signer/) and shipped in this
image as `/src/build/asan/bin/colibri-signer`). The operator supplies a
long-term signing key via config; the signer polls a Colibri server over HTTP
and parses untrusted REST and Prometheus text responses. A malicious upstream
server is the attacker here. Memory-safety or validation bugs in the signer's
HTTP response parsing are in scope.

### Where attacker-controlled bytes enter

In order of priority:

1. **SSZ proof blobs consumed by the verifier.** Entry: `c4_verify_init()` in
   [`src/verifier/verify.c`](../src/verifier/verify.c) and the JSON-status
   wrapper `c4_verify_execute_json_status()` in
   [`src/api/colibri.c`](../src/api/colibri.c), dispatched into
   Ethereum-specific verifiers under
   [`src/chains/eth/verifier/`](../src/chains/eth/verifier/). The SSZ decoder
   lives in [`src/util/ssz.c`](../src/util/ssz.c) and
   [`src/util/ssz_merkle.c`](../src/util/ssz_merkle.c). Proof types are
   declared in [`src/chains/eth/ssz/verify_proof_types.h`](../src/chains/eth/ssz/verify_proof_types.h).
2. **Patricia-Merkle and RLP proofs** carried inside those SSZ containers:
   [`verifier/patricia.c`](../src/chains/eth/verifier/patricia.c),
   [`verifier/patricia_trie.c`](../src/chains/eth/verifier/patricia_trie.c),
   [`verifier/rlp.c`](../src/chains/eth/verifier/rlp.c),
   [`verifier/eth_tx.c`](../src/chains/eth/verifier/eth_tx.c) (legacy, EIP-2930,
   EIP-1559, EIP-4844, EIP-7702 authorization lists).
3. **BLS aggregate signatures + sync-committee data**:
   [`verifier/sync_committee.c`](../src/chains/eth/verifier/sync_committee.c),
   [`verifier/sync_committee_state.c`](../src/chains/eth/verifier/sync_committee_state.c),
   [`verifier/beacon_header.c`](../src/chains/eth/verifier/beacon_header.c),
   plus checkpoint-proof cross-check and ZK-proof verification
   ([`src/chains/eth/zk_verifier/`](../src/chains/eth/zk_verifier/)). A verifier
   bug that accepts an invalid aggregate, a wrong Merkle branch, or a bogus
   checkpoint proof is Critical (see Severity below).
4. **EVM execution over state proofs** for `eth_call` / `eth_estimateGas` /
   `colibri_simulateTransaction`: [`verifier/call_evmone.c`](../src/chains/eth/verifier/call_evmone.c),
   [`verifier/verify_call.c`](../src/chains/eth/verifier/verify_call.c),
   [`verifier/verify_simulate.c`](../src/chains/eth/verifier/verify_simulate.c),
   [`verifier/state_overrides.c`](../src/chains/eth/verifier/state_overrides.c),
   plus precompiles in [`src/chains/eth/precompiles/`](../src/chains/eth/precompiles/).
5. **Common parsers used by everything above.** The zero-copy JSON parser
   ([`src/util/json.c`](../src/util/json.c),
   [`src/util/json_validate.c`](../src/util/json_validate.c)); the SSZ decoder
   mentioned above; byte/buffer helpers ([`src/util/bytes.c`](../src/util/bytes.c)).
6. **HTTP server endpoints** (only built with `HTTP_SERVER=ON`, which is on in
   this image): [`src/server/http_server.c`](../src/server/http_server.c)
   (libuv + llhttp), [`src/server/handle_proof.c`](../src/server/handle_proof.c)
   (`POST /proof`, `GET /proof/<encoded>`),
   [`src/server/handle_verify.c`](../src/server/handle_verify.c)
   (`POST /rpc`; the file is historically named `handle_verify` but serves
   `/rpc`), [`src/server/handle_unverified_rpc.c`](../src/server/handle_unverified_rpc.c)
   (`POST /unverified_rpc`), plus the registered GET handlers for `/metrics`,
   `/status`, `/version`, `/openapi.yaml`, and the ETH-chain handlers under
   [`src/chains/eth/server/`](../src/chains/eth/server/) (`/eth/v1/beacon/...`
   LCU/bootstrap paths, `/period/...` static period artifacts, `/checkpoints`,
   `/tx_cache`, and the GET block-proof handler that percent-decodes
   attacker-controlled hex blobs into `client_state` / `signers`). Attacker is
   any HTTP client that can reach the server. The optional `/config` POST
   handler (gated on `WEB_UI_ENABLED`, off by default) accepts attacker JSON.
7. **Prover-side RPC / Beacon-API responses** (JSON + SSZ returned to the
   prover): [`src/chains/eth/prover/`](../src/chains/eth/prover/) and
   [`src/chains/eth/prover/beacon.c`](../src/chains/eth/prover/beacon.c). Attacker
   is a malicious or compromised upstream node.
8. **Beacon-chain Server-Sent Events (SSE) stream** consumed by the server's
   head watcher
   ([`src/chains/eth/server/head_watcher.c`](../src/chains/eth/server/head_watcher.c)),
   which parses `text/event-stream` byte-by-byte from an untrusted beacon
   node. The watcher starts automatically with `HTTP_SERVER=ON`.
9. **Memcached response parser** in [`src/server/cache.c`](../src/server/cache.c)
   (opt-in via `memcached_host`, off by default). A malicious or compromised
   shared memcached could influence subsequent HTTP responses.
10. **Verifier-side persistent state loaded from disk** via the `FILE_STORAGE`
    plugin: `sync_<chain>_<period>` (sync-committee pubkeys loaded by
    [`verifier/sync_committee_state.c`](../src/chains/eth/verifier/sync_committee_state.c)),
    `states_<chain>` (chain state in
    [`src/verifier/verify.c`](../src/verifier/verify.c)), and
    `headers_<chain>` (EL header snapshot in
    [`verifier/header_cache.c`](../src/chains/eth/verifier/header_cache.c)).
    An attacker who can write to the states directory is the attacker role;
    severity follows the "Verifier state file corruption" rubric line. The
    EL header snapshot is self-checked (keccak-bound to `block_hash`); the
    other two are not root-checked at load time.
11. **OP-Stack chain module** (built in this image), including ZSTD-decompressed
    batcher frames: [`src/chains/op/`](../src/chains/op/).
12. **`colibri-signer` upstream parsing**
    ([`src/chains/eth/cp_signer/main.c`](../src/chains/eth/cp_signer/main.c)):
    parses JSON and Prometheus text responses from a Colibri server. A
    malicious server is the attacker.

## Project convention: validate once, trust after

Colibri follows a strict **"validate once, then trust"** convention for every
piece of attacker-controlled data (JSON RPC responses, SSZ containers,
Beacon-API payloads). An untrusted value is validated at the single point
where it enters the trust boundary; the rest of the code may read it without
re-checking the shape, type, bounds, or encoding.

Mechanics in the code:

- JSON responses from an upstream node are validated through `CHECK_JSON()` /
  `CHECK_JSON_CACHED()` against a schema expression (see
  [`src/util/json_validate.c`](../src/util/json_validate.c) and the schemas
  in [`src/chains/eth/prover/eth_req.h`](../src/chains/eth/prover/eth_req.h),
  e.g. `JSON_TX_FIELDS`, `JSON_BLOCK_HEADER_FIELDS`,
  `JSON_RECEIPTS_FIELDS`). After the check succeeds, the owning
  `data_request_t` is marked `req->validated = true` and callers that access
  the same cached value skip re-validation
  ([`src/chains/eth/prover/eth_req.c`](../src/chains/eth/prover/eth_req.c)
  is the canonical example).
- SSZ containers are validated via `ssz_is_valid()`
  ([`src/util/ssz.c`](../src/util/ssz.c)); after that `ssz_get()` /
  `ssz_at()` / typed accessors trust the layout.

Scanner implications:

- **A code path that reads a field in the shape the schema promises but is
  reachable without the validation having run is a finding at the severity
  of the downstream consequence** (verifier-wrong-result → Critical,
  memory corruption → High, DoS → Medium). "Downstream consequence" means
  the eventual effect, not the local misuse.
- A validation schema that **under-specifies** a field the code later reads
  with stricter assumptions (e.g. `CHECK_JSON("hex")` but the caller
  assumes a fixed length) is the same class of finding.
- The convention is deliberate: **do not** report "missing defensive check
  after `req->validated`" or "missing bounds check after
  `ssz_is_valid()`"; that is the convention working as designed. Report
  the missing or incomplete initial validation instead.

## Components that matter most, and what is out of scope

### Most important

- The verifier, from `c4_verify_execute_*` down to every SSZ / RLP / Patricia
  decoder and every cryptographic check reached from an attacker-controlled
  proof.
- The HTTP server (handlers, llhttp integration, JSON parsing of request
  bodies, header handling).
- Memory safety everywhere in `src/` reachable from untrusted bytes.

### Lower priority, still in scope

- Prover modules (`src/chains/eth/prover/`, `src/chains/op/prover/`). A memory
  bug in the prover affects the server operator, not the verifier user. Correct
  input handling still matters.
- The three CLI binaries (`colibri-prover`, `colibri-verifier`, `colibri-ssz`,
  entry points in [`src/cli/`](../src/cli/)). The operator chooses the
  arguments; attacker-reachable bugs come from the proof file or network
  responses they fetch, which route through the modules above. A pure
  CLI-argument bug (invalid flag, bad option combination) is Low; a bug
  reachable by the **contents** of `colibri-verifier -i <file>` or
  `colibri-ssz <file>` keeps the verifier-rubric severity, because the file
  bytes are the primary attacker-controlled input.
- The optional caches (`BLOCK_HASH_CACHE`, `EL_HEADER_CACHE`, `PROVER_CACHE`,
  logs/tx caches). Bugs that corrupt verifier-side cache entries in a way that
  changes future verification results are High; corruption that only causes a
  cache miss is a bug, not a vulnerability.
- Pragmatic Adaptive Privacy (PAP) code paths
  ([`src/chains/eth/pap.md`](../src/chains/eth/pap.md),
  `verifier/verify_pap_tx.c`, `verifier/pap_*`): same severity rules as the
  rest of the verifier.

### Out of scope

- Everything under [`libs/`](../libs/): blst, evmone, libuv, llhttp, zstd,
  tommath, intx, mcl, and the vendored `libs/crypto/` primitives (AES, Keccak,
  SHA, Ed25519-donna). These are upstream projects with their own review
  processes. Report a bug there only if **our code calls into them with
  invariants they do not promise to uphold** (wrong length, aliased buffers,
  unchecked return code, misuse of a constant-time API, …). In that case the
  finding is about `src/` violating its contract with `libs/`, not about
  `libs/` itself.
- The protocol-level assumptions in
  [`src/chains/eth/threat_model.md`](../src/chains/eth/threat_model.md):
  honest ≥⅔ of sync committee, honest weak-subjectivity anchor, reorg
  probability. These are documented security assumptions, not bugs. A report
  that merely restates them is not a finding.
- The Rust / SP1 ZK **host** build (`src/chains/eth/zk_proof/`, Cargo crates,
  SP1 prover). This image copies the frozen `eth_sync_program` guest ELF but
  does **not** build the host (`ETH_ZKPROOF_BUILD_HOST=OFF`, no Rust toolchain
  installed), so SP1 and its dependencies are not in scope here. The on-chain
  **verifier** side of ZK proofs ([`src/chains/eth/zk_verifier/`](../src/chains/eth/zk_verifier/))
  **is** in scope and in the build.
- The Kona-P2P bridge ([`src/chains/op/kona_bridge/`](../src/chains/op/kona_bridge/)):
  not built in this image (`BUILD_KONA_BRIDGE=OFF`, no Rust toolchain).
- Language bindings under [`bindings/`](../bindings/) (Emscripten/WASM,
  Node-addon, Python, Kotlin, Swift, Dart, Rust). Not built here. The C API
  they wrap is in scope through `src/api/`.
- The web UI assets under [`src/server/web_ui/`](../src/server/web_ui/) (HTML,
  CSS, JS). The server endpoints that back them are in scope; the static
  assets are not.
- Everything in [`test/`](../test/) ([`test/unittests/`](../test/unittests/),
  [`test/embedded/`](../test/embedded/), [`test/data/`](../test/data/),
  [`test/valgrind/`](../test/valgrind/)): assertion failures and sanitizer
  reports **inside a test harness** are the harness's job to signal. If a test
  reveals a bug in `src/`, the finding is about the `src/` code.
- Build system ([`CMakeLists.txt`](../CMakeLists.txt), presets, installer
  scripts under [`installer/`](../installer/), workflow files under
  [`.github/`](../.github/)), docs under [`scripts/doc/`](../scripts/doc/) and
  [`scripts/`](../scripts/) in general.
- Side-channel attacks that depend on physical access, EM/power analysis,
  speculative execution (Spectre family), or SMT co-residency. Classic timing
  side channels on secret-dependent branches / memory indices in cryptographic
  code paths **are** in scope, but Colibri does not hold long-term secrets:
  the verifier only knows a (public) sync committee, and server operator keys
  (checkpoint signer, if any) are generated and used by `cp_signer`
  separately.
- 32-bit builds. This image is x86-64 only. If you find an issue that only
  bites on 32-bit `size_t`, note it in the report and do not reproduce or rate
  it as 64-bit.
- Comments documenting protocol specification. The project treats
  specification-aligned comments (lines beginning with `// :`, `// ::`,
  `// :::`) as frozen; see
  [`AGENTS.md`](../AGENTS.md). A "fix" that only rewords such a comment is not
  a finding and will not be merged.

## How to exercise it

### The image: one tree, under /src/build/asan

Clang `-O1 -g -fno-omit-frame-pointer`, AddressSanitizer + UndefinedBehavior
sanitizer with `-fno-sanitize-recover=all`, built as `RelWithDebInfo` to keep
debug info but let the optimiser surface more real bugs. CMake preset flavour
is the project's `default`: `CHAIN_ETH=ON`, `CHAIN_OP=ON`, `HTTP_SERVER=ON`,
`PROVER_CACHE=ON`, `PROVER_TRACE=ON`, `CLI=ON`, `EVMONE=ON`, `ETH_ZKPROOF=ON`
(verifier side only), `TEST=ON`. The frozen SP1 guest ELF is copied beside the
binaries; the SP1 host is not built.

The checkout is at `/src`; `git -C /src rev-parse HEAD` names the commit under
scan.

### Binaries worth driving

All under `/src/build/asan/bin/`:

- `colibri-verifier` — verify an SSZ proof blob against a checkpoint. Reads a
  proof with `-i proof.ssz` and the method name + args. This is the primary
  attacker-facing entry point from a user application; a bad proof must either
  be rejected cleanly or produce the correct result.

  The verifier CLI supports three prover modes selectable via `-m`:
  - `remote` (default when `-p` / prover URL is set): the proof is fetched
    from an external prover; attacker-controlled bytes come from that
    response.
  - `local` (`-m local`, `-L`, or default when `-i` is used): the prover
    runs **in the same process as the verifier**, hitting the configured RPC
    and Beacon-API endpoints directly. In this mode the entire prover
    surface ([`src/chains/eth/prover/`](../src/chains/eth/prover/),
    [`src/prover/`](../src/prover/)) runs inside the user application and
    attacker-controlled bytes also enter through those RPC / Beacon-API
    responses, exactly as on the server.
  - `hybrid` (`-m hybrid`): mixes the two; parts of the data come from a
    remote prover and parts are built locally.
  A bug reachable only via local- or hybrid-mode is a user-application bug
  (the user's own node choice is in play), not a server bug. It is still in
  scope.

  Reproducer example:
  ```sh
  /src/build/asan/bin/colibri-verifier -i /tmp/evil.ssz eth_getBlockByNumber latest false
  ```
- `colibri-prover` — generate a proof. Fetches RPC + Beacon data. **Needs
  network**, which the scanner has disabled. Use it only to re-generate a
  reproducer from the test fixtures (which are included and offline).
- `colibri-ssz` — decode SSZ to JSON / compute `hash_tree_root`. Useful as an
  oracle while writing a reproducer. Reads a file, writes JSON. Pure parser
  surface, no network.
- `colibri-server` — the HTTP server. Starting it in the scanner's offline
  sandbox needs a bit of setup: on boot the server probes every configured
  RPC endpoint (via `c4_server_handlers_get_detection_request` →
  `eth_get_detection_request`) and every Beacon API node to detect the client
  type (`http_servers_select.c:952`), reads defaults from
  [`src/server/configure_server.c`](../src/server/configure_server.c)
  (`rpc_nodes = "https://eth.llamarpc.com,..."` and
  `beacon_nodes = "https://ethereum-beacon-api.publicnode.com,..."`), and, if
  the operator set `memcached_host` (empty by default, but typical in prod
  deployments), opens the memcached connection pool. All of those fail hard
  without network. To exercise the HTTP server's parsing and routing paths
  offline the easiest options are (a) point `rpc_nodes` / `beacon_nodes` at
  file URLs served by a mock (see `test/data/server/` fixtures and the
  `TEST_DIR` recording mode in `configure_server.c:168`), (b) use the
  `test_server*` unit tests which bind the server to `127.0.0.1` and feed it
  recorded responses, or (c) run `colibri-server --help` to confirm the
  binary links and then exercise the handler bodies via the C API or a
  freshly written driver.

### Unit tests

All test binaries land in `/src/build/asan/test/unittests/test_<name>`. They
auto-run via `ctest --test-dir /src/build/asan`. **Run `-j1` (serial).**
Several server/libuv tests bind to fixed localhost ports and interfere with
each other under parallel execution; the smoke step in the Dockerfile
already enforces this. The tests mock external HTTP via file URLs pointing
at fixtures under [`test/data/`](../test/data/) (see
`test/unittests/file_mock_helper.h`), so the whole suite runs offline. See
[`test/AGENTS.md`](../test/AGENTS.md) for the list.

- Verifier reproducers: `test_eth_verify_*`, `test_eth_sync`, `test_eth_ssz*`,
  `test_eth_patricia_trie`, `test_eth_precompiles`, `test_bn254`,
  `test_zk_proof`.
- Prover reproducers: `test_pap_*`, `test_transaction_cache`, `test_logs_cache`,
  `test_state_proofs`, `test_eth_sync`.
- Server reproducers: `test_server*`, `test_http_client_errors`,
  `test_beacon_watcher`, `test_uv_util`. These spawn the server or use the
  libuv event loop; the smoke step in the Dockerfile excludes them to keep
  the build fast but they are available during the scan.

Writing a focused reproducer from a crash:

```sh
# Build is already done; just compile a small driver against the installed headers.
cat <<'C' > /tmp/repro.c
#include <stdio.h>
#include "api/colibri.h"
int main(int argc, char** argv) {
    /* open argv[1], call c4_verify_create_ctx(...), c4_verify_execute_json_status(...) */
}
C
clang -g -O1 -fsanitize=address,undefined -I/src/src /tmp/repro.c \
    -L/src/build/asan -lapi -lverifier -leth_verifier -lutil \
    -o /tmp/repro
/tmp/repro /tmp/evil.ssz
```

Or just extend an existing `test_*.c` under `test/unittests/`; it is picked up
by CMake automatically on the next build.

### Fixtures

[`test/data/<name>/`](../test/data/) contains real recorded RPC / Beacon
responses and the SSZ blobs produced from them. They are a corpus: start from a
fixture, mutate the SSZ bytes, feed it to `colibri-verifier -i`. Good starting
points by surface:

- Account / storage / proof: `eth_getBalance1`, `eth_getProof1`, `eth_getStorageAt1`.
- Transactions: `eth_getTransactionByHash1`, `eth_getTransaction_Type_4` (EIP-7702).
- Receipts and logs: `eth_getTransactionReceipt1`, `eth_getLogs1`, `log_cache/`.
- Block headers: `eth_getBlockByNumber1`, `eth_getBlockHeader1`,
  `trusted_block1/`.
- `eth_call` and simulation: `eth_call1`, `eth_call_7702`, `simulate_simple/`.
- Sync committee + ZK: `eth_sync/`, `zk_data/`.
- OP-Stack: `op_eth_blockNumber/`, `op_eth_getBlockByNumber/`.
- PAP: `pap_tx_*`, `eth_getLogs_pap1/`.
- Server: `test/data/server/`.

### Debug tooling in the image

- `gdb` for backtraces (ASan already prints a decoded one, but `gdb -ex r --args
  /src/build/asan/bin/colibri-verifier -i /tmp/evil.ssz …` works for stepping).
- `ASAN_OPTIONS=detect_leaks=0` is useful when running inside the Dockerfile
  smoke step (no ptrace); drop it during the scan to see leaks.

## How we rate severity

Severity is driven by **what the attacker gains**, not by the bug class alone.
Always state the attacker role and the public entry point in the report.

### Critical

- **Verifier accepts an invalid proof** and returns a result that disagrees
  with canonical Ethereum state (any `eth_*` method, including transaction
  existence, receipts, logs, account state, `eth_call`, block headers,
  simulate). The attacker is a malicious prover; the victim is a wallet or
  embedded verifier.
- **Verifier accepts an invalid sync-committee update** (wrong BLS aggregate,
  wrong finality header, forged `nextSyncCommittee`, bypassed checkpoint
  cross-check, bypassed WSP cross-check in `USE_CHECKPOINTZ=ON` builds,
  bypassed ZK-proof verification, forged EIP-191 witness signature on the
  `WITNESS_SIGNER` path at
  [`verifier/sync_committee.c`](../src/chains/eth/verifier/sync_committee.c),
  or any implementation bug that moves the `c4_check_weak_subjectivity`
  cross-check after `c4_set_sync_period` has persisted the new committee).
  Same attacker, same victim.
- **Memory corruption reachable from the verifier's untrusted inputs** (SSZ
  bytes, Patricia proofs, RLP, BLS public keys, ZK proof bytes) with a
  plausible path to code execution.
- **Memory corruption in the HTTP server** reachable from a POST body or
  header, on a default-configured server listening on a network interface.
- A single malicious upstream RPC/Beacon response that causes the **prover to
  construct a proof the verifier accepts for a wrong result**. This is the
  same guarantee as above, flipped: the attacker only needs to control one
  RPC node a server operator happens to use.
- **Poisoning of `BLOCK_HASH_CACHE`** (default-on,
  [`verifier/beacon_header.c`](../src/chains/eth/verifier/beacon_header.c)):
  this cache short-circuits per-block BLS sync-aggregate verification. Today
  it is only populated after `blst_verify` succeeds, so poisoning requires a
  separate write path (e.g. an SSZ-decoder OOB write into `blockhash_cache`
  or a logic bug that promotes an unverified root). Any such path yields
  "verifier accepts an arbitrary blockroot", which is a direct bypass of
  sync-committee verification.

### High

- Memory corruption reachable from untrusted input (verifier or server),
  without a demonstrated exploit path.
- Verifier accepts an invalid proof in a non-default configuration (e.g.,
  `VERIFY_FLAG_SKIP_WSP_CHECK` set, `USE_CHECKPOINTZ=OFF` without an explicit
  checkpoint). Say which flag is required and why a real deployment might set
  it.
- Prover produces a proof whose SSZ encoding is corrupt in a way that triggers
  a verifier bug — rate as a verifier Critical / High plus a prover note.
- Cache poisoning (`PROVER_CACHE`, `EL_HEADER_CACHE`, `logs_cache`,
  `tx_cache`) that persists across requests and changes future verification
  results. If a poisoned cache entry flows into a proof that the verifier
  later accepts for a wrong result, promote to Critical (same reasoning as
  "malicious upstream RPC response"). `BLOCK_HASH_CACHE` poisoning is
  Critical on its own and listed under Critical above.
- Verifier state file corruption (`FILE_STORAGE`) that persists across
  restarts and lets a later proof be forged.

### Medium

- Crash, infinite loop, or algorithmic blow-up (super-linear CPU, unbounded
  memory) in the verifier or server from untrusted input. Pure DoS, no state
  corruption, no incorrect result. Linear-in-input inefficiency is not a
  finding.
- Null-pointer dereference or integer over/underflow with a clear source but
  no demonstrated memory-safety consequence.
- Timing-oracle leak in a cryptographic path that handles verifier-held
  secrets. Colibri has few of these (public sync committee, no per-user
  keys), so this is usually Low or a non-finding.

### Low

- Bug only reachable through the three CLI binaries' command-line arguments,
  a config file, or an environment variable the local operator controls.
- Bug that requires a server operator to set a documented-unsafe flag
  (`PROXY_ENABLED=1` on `/proof`, `VERIFY_FLAG_SKIP_WSP_CHECK`,
  `USE_CHECKPOINTZ=OFF`) in a way the docs warn against.
- Cache or state corruption that reliably triggers a cache miss / state
  reset but never produces a wrong result.
- Failure to zeroize a buffer, unless there is a documented promise to
  zeroize that is broken.

### Not a finding ("just a bug" or "not even a bug")

- Bug reachable only by calling a C API with arguments the documentation
  forbids (null pointer where `NONNULL` says non-null, length that disagrees
  with the pointer, aliased buffers where disallowed). Document contract
  violations are the caller's problem.
- An invalid input that triggers a clean `C4_ERROR` with a message that
  describes why. That is the designed behaviour. Only crashes, undefined
  behaviour, memory corruption, infinite loops, unbounded allocation, or
  wrong results delivered without `C4_ERROR` are interesting.
- Protocol-level complaints already covered in
  [`src/chains/eth/threat_model.md`](../src/chains/eth/threat_model.md)
  (sync-committee bribery cost, long-range attacks, reorg risk).
- A `libs/` upstream bug that the Colibri `src/` tree uses correctly.
- Style, naming, missing const, unused include, unused variable, comment
  typo. The project runs clang-format and clang-tidy equivalents separately.
- Any change to a `// :` / `// ::` / `// :::` specification comment that
  rewords the spec, or any removal of information from an existing comment.
  These are frozen by project policy ([`AGENTS.md`](../AGENTS.md)).

## How we would like reports and patches to look

One finding per report. Include:

- The commit (`git -C /src rev-parse HEAD`).
- Which attacker role controls the input (malicious prover, malicious RPC/Beacon
  node, HTTP client, local operator, writer of the states directory, upstream
  server for `colibri-signer`) and through which public entry point the bytes
  arrive (`c4_verify_execute_*`, `colibri-verifier -i`, `POST /proof`,
  `POST /rpc`, `POST /unverified_rpc`, one of the ETH-chain GET handlers, the
  SSE stream in `head_watcher.c`, a storage-plugin key loaded on verifier
  init, …).
- The exact reproducer command (`/src/build/asan/bin/colibri-verifier -i … …`
  or a `ctest -R test_<name>` invocation or a small C driver with its compile
  line) and the raw ASan / UBSan output.
- Which severity rubric line applies, and why.
- A minimal patch against `dev` (default branch), following the project's
  coding conventions in [`AGENTS.md`](../AGENTS.md): English comments only in
  the code, `TRY_ASYNC()` / `THROW_ERROR()` for the state machine, no new
  dependencies, `safe_malloc` / `safe_free` for allocations. **Do not touch
  spec-facing comments.** Add a regression test under `test/unittests/` using
  the Unity pattern; put binary fixtures under `test/data/<name>/`.
