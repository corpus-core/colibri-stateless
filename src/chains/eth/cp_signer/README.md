## External Checkpoint Signer (`colibri-signer`)

This directory contains `colibri-signer`, a reference implementation of an **external signer** that signs finalized Ethereum Beacon checkpoints and submits the signatures back to the Colibri server.

If you want to implement your own signer (any language / environment), the most important parts are the **HTTP API** and the **signature specification** below.

### Goal

- Enable many independent developers/operators to run their own checkpoint signers (servers, HSM-backed signers, wallets, cloud functions, offline devices, etc.).
- Provide a simple **pull → verify → sign → push** interface.

### Terms

- **Checkpoint root**: 32-byte root (hex) that must be signed. It is the SSZ `hash_tree_root` of the `BeaconBlockHeader`.
- **Signer address**: Ethereum address (20 bytes) derived from the secp256k1 signing key.

---

## HTTP API (Server)

### 1) List unsigned checkpoints

`GET /signed_checkpoints?signer=0x<address>`

Response: JSON array; each item describes a checkpoint that should be signed:

```json
[
  {
    "period": 1621,
    "slot": 13292352,
    "root": "0x76fb0f621ff30c53869de9fe9ebd498eb041be04b49b15284a4cc614d37d0971"
  }
]
```

Notes:
- `period` is the server-side identifier (period store directory).
- `slot` exists for **external validation**.
- `root` is the 32-byte checkpoint root that must be signed.

### 2) Submit signatures

`POST /signed_checkpoints`

Body: JSON array, one object per signature:

```json
[
  {
    "period": 1621,
    "signature": "0x<130-hex-chars-plus-0x-prefix>"
  }
]
```

Signature format:
- `signature` is a **65-byte recoverable secp256k1 signature** (R,S,V) as a hex string.
- The server recovers the signer address from the signature and stores it under `sig_<address>`.

---

## Signature specification (EIP-191 / `personal_sign`)

The signer signs **only** the 32-byte `checkpoint_root` (not JSON, not an SSZ object), but using the EIP-191 message prefix (as used by `personal_sign`).

### Message Digest

```text
digest = keccak256("\x19Ethereum Signed Message:\n32" || checkpoint_root_32bytes)
```

Then `digest` is signed using **secp256k1** (recoverable, 65 bytes).

### Minimal workflow for custom signers

1. `GET /signed_checkpoints?signer=0x...`
2. For each item:
   - **Verify** that `root` is correct and **finalized** (see next section).
   - Compute `digest` as shown above.
   - `signature = secp256k1_sign_recoverable(digest)`
3. `POST /signed_checkpoints` with `{period, signature}`.

---

## Recommendation: verify root & finality (Beacon API / checkpointz)

The server provides `slot` and `root`, but an external signer should **not sign blindly**.

Recommended checks:

### A) Root matches the slot (canonical root)

`GET /eth/v1/beacon/blocks/<slot>/root`

Expected structure:

```json
{ "data": { "root": "0x..." } }
```

Note: Some services (e.g. `checkpointz`) may return `root` **without** the `0x` prefix. A robust implementation should accept both.

### B) Block is finalized

Preferred (Beacon API, not always supported by checkpointz):

`GET /eth/v1/beacon/headers/0x<root>`

Expected fields:
- `finalized == true`
- `data.canonical == true`
- `data.root` matches the requested root

Fallback (checkpointz-compatible):

`GET /eth/v2/beacon/blocks/<slot>`

Expected fields:
- `finalized == true`

If `finalized` is not true: **do not sign** (try again later).

---

## Reference CLI: use `colibri-signer`

Example (local Colibri server + local Beacon node):

```bash
build/default/bin/colibri-signer \
  --server http://localhost:8090 \
  --key-file ./cp_key \
  --beacon-api http://localhost:5052 \
  --once
```

Optionally you can provide nodes explicitly (overrides/extends the curl config):

```bash
build/default/bin/colibri-signer \
  --server http://localhost:8090 \
  --key 0x<32-byte-hex-private-key> \
  --checkpointz https://sync-mainnet.beaconcha.in \
  --beacon-api https://lodestar-mainnet.chainsafe.io \
  --once
```

---

## Curl quickstart (API-only)

1) Fetch checkpoints to sign:

```bash
curl "http://localhost:8090/signed_checkpoints?signer=0x0123456789abcdef0123456789abcdef01234567"
```

2) Submit signature:

```bash
curl -X POST "http://localhost:8090/signed_checkpoints" \
  -H "Content-Type: application/json" \
  -d '[{"period":1621,"signature":"0x..."}]'
```

