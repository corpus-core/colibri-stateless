# Arbitrum One: Block Verification Strategy for colibri-stateless

Status: draft, 2026-11-16 (rev 6: signer validity window on the existing freshness gate)
Scope: how a colibri verifier establishes trust in an Arbitrum One (chain id 42161) block hash, and what differs from Ethereum mainnet once the block hash is trusted.

Everything colibri proves (accounts, storage, transactions, receipts, logs, calls) hangs off a verified execution block hash. This document defines how that hash is proven on Arbitrum One, and what each milestone delivers:

- **Milestone 1:** the head and the last 27 hours, by sequencer feed signature (section 2).
- **Milestone 2:** L1 anchor for `finalized`-grade proofs (section 3).
- **Milestone 3:** execution of Stylus contracts (sections 5.7 and 8).
- **Milestone 4:** historic blocks older than 27 hours (section 4).

Milestones 2 to 4 each build on Milestone 1 and are independent of one another. The two block-proof tiers compared:

| | Milestone 1: sequencer feed signature | Milestone 2: L1 anchor |
|---|---|---|
| Trust | Arbitrum sequencer key (same as every RPC user today) | Ethereum sync committee + Arbitrum rollup contracts |
| Freshness | head of chain (sub-second) | roughly one challenge period behind head (about a week) for confirmed assertions |
| Serves tags | `latest`, block numbers within the last 393168 blocks (about 27 h) | `finalized`-grade proofs, historic blocks |
| New proof variant | `arbFeedProof` (index 6) | `arbL1Proof` (index 7, sketch) |

Items marked **[verify]** were not confirmed against source or chain while writing this and must be checked during implementation. Section 9 collects them.

---

## 1. Background

Arbitrum One has a single sequencer. For each sequenced message it produces exactly one L2 block and publishes the message on a WebSocket feed (`wss://arb1.arbitrum.io/feed`) before the data reaches L1. Later the batch poster posts compressed batches to the `SequencerInbox` on L1, and validators post assertions about the resulting state to the rollup contract, which are confirmed after the challenge period.

Three points in that pipeline can anchor a block hash:

1. **Feed signature** (this document, M1). The feed message carries the L2 block hash and an ECDSA signature that covers it.
2. **Pending assertion** on L1 (optional later tier). Bonded but still challengeable.
3. **Confirmed assertion** on L1 (M2). The confirmed block hash is written to L1 contract storage.

L1 batches alone do not help a stateless verifier: they commit to inputs, not to a block hash, so using them requires re-executing the chain.

---

## 2. Milestone 1: sequencer feed signature

### 2.1 What the sequencer signs

Source of truth: `BroadcastFeedMessage.SignatureHash` in Nitro, `broadcaster/message/message.go`. The reimplementation in `arb-feed-sigs.js` reproduces Nitro's test vector and recovers one stable signer on live Arbitrum One traffic.

The feed message as JSON:

```json
{
  "sequenceNumber": 502600783,
  "message": {
    "message": {
      "header": {
        "kind": 3,
        "sender": "0xa4b000000000000000000073657175656e636572",
        "blockNumber": 26428992,
        "timestamp": 1794849013,
        "requestId": null,
        "baseFeeL1": 0
      },
      "l2Msg": "<base64>"
    },
    "delayedMessagesRead": 2621089
  },
  "blockHash": "0xc71d3286d54d63dcdbd08b929947198c474e7558f96021c4d6bfbe01b3432e73",
  "signatureV2": "<base64, 65 bytes>",
  "blockMetadata": "AAA="
}
```

The signed digest is `keccak256` over this flat concatenation. There are no length prefixes and no RLP.

| # | Field | Size | Encoding | Source |
|---|---|---|---|---|
| 1 | prefix | 20 | ASCII `Arbitrum Nitro Feed:` | constant |
| 2 | chainId | 8 | uint64 big-endian | 42161 |
| 3 | sequenceNumber | 8 | uint64 big-endian | `sequenceNumber` |
| 4 | blockHash | 32 or 0 | raw | `blockHash`, omitted if absent |
| 5 | blockMetadata | variable | raw bytes | `blockMetadata` |
| 6 | delayedMessagesRead | 8 | uint64 big-endian | `message.delayedMessagesRead` |
| 7 | kind | 1 | uint8 | `header.kind` |
| 8 | poster | 20 | raw address | `header.sender` |
| 9 | l1BlockNumber | 8 | uint64 big-endian | `header.blockNumber` (an L1 block number) |
| 10 | timestamp | 8 | uint64 big-endian | `header.timestamp` |
| 11 | requestId | 32 or 0 | raw | `header.requestId`, omitted if null |
| 12 | l1BaseFee | 0..32 | big-endian, no leading zeros | `header.baseFeeL1`, omitted if null, empty if 0 |
| 13 | l2Msg | variable | raw bytes | `message.message.l2Msg` |

Signature: 65 bytes `r(32) | s(32) | v(1)` with `v` in {0, 1}, secp256k1 directly over the digest. No EIP-191 prefix.

L2 block number: `sequenceNumber + 22207817` on Arbitrum One (22207817 is the Nitro genesis block).

Two consequences for the proof format:

- The digest cannot be computed without the full message, including `l2Msg` (all transactions of the block; 5.4 kB in the sample above). The proof size is therefore about one block of transactions, comparable to the OP-Stack payload proof.
- Fields 1 to 4 are fully determined by the verifier's chain config and the L2 header. Everything from field 5 on is opaque to the block hash check.

### 2.2 Proof format: `arbFeedProof`

New variant appended to `ETH_EL_PROOF_UNION` at index 6. Existing indices stay frozen.

```c
// Arbitrum sequencer feed proof (Nitro `signatureV2`).
//
// digest = keccak256( "Arbitrum Nitro Feed:"            // 20 bytes
//                   | be64(chainId)                      // verifier chain config
//                   | be64(number(elHeader) - genesis)   // feed sequence number
//                   | keccak256(elHeader)                // the L2 block hash
//                   | signedTail )
//
// `signedTail` is everything that follows `blockHash` in Nitro's SignatureHash:
//   blockMetadata | be64(delayedMessagesRead) | kind(1) | poster(20) | be64(l1BlockNumber)
//   | be64(timestamp) | [requestId(32)] | [l1BaseFee] | l2Msg
static const ssz_def_t ARB_FEED_PROOF[] = {
    SSZ_PROG_BYTES("elHeader"),                           // RLP-serialized L2 execution header
    SSZ_UNION("signedTail", ETH_SEQUENCER_PAYLOAD_UNION), // ZSTD-compressed or uncompressed tail
    SSZ_BYTE_VECTOR("signature", 65),                     // secp256k1 (r, s, v), v in {0,1} or {27,28}
};

static const ssz_def_t ETH_EL_PROOF_UNION[] = {
    ...                                                   // 0..5 unchanged
    SSZ_CONTAINER(ETH_SSZ_NAME_ARB_FEED_PROOF, ARB_FEED_PROOF), // 6: Arbitrum sequencer feed signature
};
```

Design decisions:

- **Opaque tail instead of structured fields.** The verifier never needs `kind`, `poster` or `l2Msg` to authenticate the block hash. Passing the tail as bytes keeps the C verifier to one streaming keccak with no parsing and no allocation, and it survives Nitro appending fields behind `blockHash`.
- **`blockHash` and `sequenceNumber` are not in the proof.** Both are derived from `elHeader`. A feed message signed without a block hash can therefore never verify (see 7.4).
- **Reuse of `ETH_SEQUENCER_PAYLOAD_UNION`** for optional ZSTD compression, as with OP-Stack.
- **No signer material in the block proof.** The proof of who may sign travels in `sync_data` of the `C4Request`, exactly like sync committee updates (2.4). A block proof stays small and cacheable.

### 2.3 Verification algorithm

Input: chain config (`chainId`, `genesisBlock = 22207817`, trusted signer set, `SequencerInbox` address), proof.

1. Parse `elHeader` as RLP. Read `number`. Reject if `number < genesisBlock` (pre-Nitro blocks have no feed).
2. `blockHash = keccak256(elHeader)`.
3. `seq = number - genesisBlock`.
4. Decompress `signedTail` if needed. Reject if shorter than 45 bytes (the fixed-size fields 6 to 10).
5. `digest = keccak256(prefix | be64(chainId) | be64(seq) | blockHash | signedTail)`.
6. Normalize `v` (accept 0/1 and 27/28), `ecrecover(digest, signature)` to get `signer`.
7. Accept if `signer` equals the trusted signer in the verifier's sync state and that signer has not expired (signer validity window, 2.4). If the request carries `sync_data` with a signer update, that update is verified and stored first. An unknown or expired signer without a valid update is an error; the client then retries with an empty signer in its `client_state`.
8. Return the verified header. `stateRoot`, `transactionsRoot` and `receiptsRoot` are used as on mainnet.

Optional freshness check for `latest`: reject if the header timestamp is older than a configurable maximum age compared with the local clock (7.5).

Worked example (first message of the capture): `seq = 502600783`, `number = 524808600`, `blockHash = 0xc71d…2e73`, tail = `0x0000 | be64(2621089) | 0x03 | a4b0…6572 | be64(26428992) | be64(1794849013) | (no requestId) | (empty base fee) | l2Msg`. Recovered signer: `0xC1b634853Cb333D3aD8663715b08f41A3Aec47cc`.

### 2.4 Which signer is the right one

The feed is signed with the batch poster's L1 wallet key: in Nitro, the feed's `dataSigner` is opened from `node.batch-poster.parent-chain-wallet`. `0xC1b634853Cb333D3aD8663715b08f41A3Aec47cc` is the address that submits Arbitrum One batches to the `SequencerInbox` (`0x1c479675ad559DC151F6Ec7ed3FbF8ceE79582B6`) and is labelled "Arbitrum: Batch Submitter" on Etherscan.

The address is not fixed by the protocol. It is an entry in `SequencerInbox.isBatchPoster`, which the rollup owner or the `batchPosterManager` can change at any time. Nitro nodes themselves accept a feed signer if it is on a local allow-list or if the L1 contract says it is a batch poster or sequencer (`IsBatchPosterOrSequencer`).

The signer is part of the verifier's **sync state**, next to the sync committee keys, and is updated the same way: the verifier tells the prover what it holds, and the prover adds an update to `sync_data` when that is missing or outdated.

**Request flow**

1. The verifier sends its `client_state` with the request. For Arbitrum it additionally contains the trusted feed signer (20 bytes) and the number and timestamp of the L1 block at which it was proven. A fresh verifier sends an empty signer.
2. The prover attaches a signer update as `sync_data` if the field is empty, if it differs from the signer of the block it is about to prove, or if the stored L1 proof is older than the signer validity window (see below).
3. The verifier processes `sync_data` first, stores the new signer, then verifies the block proof against it.

**Sync data format.** New variant appended to `C4_ETH_REQUEST_SYNCDATA_UNION` (index 3):

```c
// Proves that `signer` is a registered batch poster of the chain's SequencerInbox on L1.
// No EVM call is involved: the mapping entry is read straight from contract storage.
//
//   account.address         = SequencerInbox of the chain (from chain config)
//   account.accountProof    -> proves the account, and with it `storageHash`, against the L1 `stateRoot`
//   account.storageProof[0] -> key   = keccak256(pad32(signer) || pad32(3))    // mapping isBatchPoster, slot 3
//                              value = 1
//   account.elProof         = clProof of the L1 block (sync-committee signed)
static const ssz_def_t ARB_SIGNER_SYNCDATA[] = {
    SSZ_ADDRESS("signer"),                       // the feed signer to be trusted
    SSZ_CONTAINER("account", ETH_ACCOUNT_PROOF), // L1 account + storage proof of SequencerInbox.isBatchPoster[signer]
};

const ssz_def_t C4_ETH_REQUEST_SYNCDATA_UNION[] = {
    SSZ_NONE,
    SSZ_CONTAINER(ETH_SSZ_NAME_LC_SYNC_DATA, ETH_LC_SYNCDATA),          // 1: unchanged
    SSZ_CONTAINER(ETH_SSZ_NAME_ZK_SYNC_DATA, ETH_ZK_SYNCDATA),          // 2: unchanged
    SSZ_CONTAINER(ETH_SSZ_NAME_ARB_SIGNER_SYNC_DATA, ARB_SIGNER_SYNCDATA), // 3: Arbitrum feed signer, proven on L1
};
```

**Verification of the update**

1. `account.address` must be the `SequencerInbox` configured for the chain.
2. `account.elProof` must be a `clProof` of an L1 block; a cached block hash is not accepted. This reuses the L1 sync committee state the verifier holds for the parent chain, as the EEZ module does.
3. Verify `accountProof` against the L1 `stateRoot`. This yields the contract's `storageHash`.
4. `storageProof` must hold exactly one entry. Its key must equal `keccak256(pad32(signer) || pad32(3))`, recomputed by the verifier from `signer`. Verify the storage proof against `storageHash`.
5. The value must be exactly 1. A proof of absence (value 0) is rejected.
6. The L1 block must be recent: `l1BlockTimestamp + W >= ctx->min_latest_block_ts`. Without this check a replayed proof from before a key was removed would be accepted.
7. Store `signer` with the number and timestamp of the L1 block in the sync state. Reject an update whose L1 block is older than the one already stored.

For the current signer the storage key is `0xa10aa54071443520884ed767b0684edf43acec528b7da83ab38ce60126562660`. Slot 3 is derived from the contract source (`totalDelayedMessagesRead`, `bridge`, `rollup`, `isBatchPoster`) and is **[verify]** against the deployed proxy. Nitro also accepts addresses in `isSequencer` (slot 9); the format can cover that later with a slot selector, it is not needed while the batch poster signs.

**Notes**

- **L1 sync inside L2 sync.** If the verifier's L1 sync committee state is itself outdated, the L1 light client update has to be delivered first. One `sync_data` union value cannot carry both, so either the client syncs L1 in a first round trip (as today) or `sync_data` becomes a list. A round trip is the smaller change.
- **No pinned signer.** A signer shipped in the chain config would have no L1 proof time and could never expire, which defeats the validity window below. The verifier trusts only L1 and the configured `SequencerInbox` address; the first request of a fresh verifier always carries a signer update.
- **Depth of the L1 block.** A recent head block is preferable to a finalized one here, because freshness is what the proof is for. An L1 reorg of the head cannot make a removed key valid again for longer than the window.

**Signer validity window**

The verifier does not notice a rotation by itself. It learns of it when blocks arrive that are signed by a new key. An attacker who holds a removed key and controls the verifier's connection can simply keep serving blocks signed with the old key, and a verifier that was last active a month ago would accept them. This is the same problem as weak subjectivity for the sync committee, and it gets the same kind of answer.

Rule: **a signer is trusted only until `l1ProofTime + W`**, where `l1ProofTime` is the timestamp of the L1 block in its last proof and `W` is the signer validity window (default 27 h, configurable).

- The deadline derives from the L1 block's own timestamp, not from the moment the verifier received the proof. Replaying an old proof therefore buys nothing: it is either older than the stored one or already past its deadline.
- Before accepting an `arbFeedProof`, the verifier compares its clock with the deadline. If the signer has expired, the block proof is rejected unless the request carries a fresh signer update. The prover sees the stored L1 timestamp in `client_state` and attaches the update without a further round trip.
- An active verifier thus refreshes about once per window. The update is one L1 header proof plus an account and a storage proof, a few kB.
- Exposure after a key is removed is at most `W`, and only for verifiers whose connection the attacker controls. Applications that need less can lower `W`; `W = 0` means every request carries a signer proof and the verifier keeps no signer state at all.
- **Clock: reuse of the existing freshness gate.** colibri already has a time reference: the host sets `ctx->min_latest_block_ts` (the oldest block timestamp it accepts for `latest`), and `eth_check_latest_freshness` compares header timestamps against it. The signer check uses the same value as its clock: the signer is expired if `l1ProofTime + W < ctx->min_latest_block_ts`.
- **The check applies to every `arbFeedProof`, not only to `latest`.** For `latest`, the existing gate plus `header.timestamp <= l1ProofTime + W` would already be enough, because the header must be fresh and inside the signer's lifetime at once. For an explicit block number the freshness gate does not run, and an attacker holding a removed key could back-date the header of a forged block into the signer's lifetime. Comparing the deadline with `min_latest_block_ts` directly closes that, independent of the block tag and of the attacker-chosen header timestamp.
- **If the host disables the gate** (`min_latest_block_ts == 0`), the verifier has no time reference and cannot expire a signer. This should be documented as an explicit downgrade, and the EEZ-style exception inside `eth_check_latest_freshness` must not be extended to Arbitrum.

### 2.5 Prover (remote proof) design

The feed is a stream and cannot be queried per block, so M1 needs the remote prover.

- Keep one feed connection (headers: `Arbitrum-Feed-Client-Version: 2`, optional `Arbitrum-Requested-Sequence-Number`; the server negotiates permessage-deflate).
- For each message: compute the digest, recover the signer, drop messages without `blockHash`, without `signatureV2`, or with an unknown signer.
- Keep the newest verified message in memory (plus a few predecessors so in-flight requests do not race). Nothing is persisted in Milestone 1.
- **`latest` = newest block that has a valid signature and is available on the prover's own L2 node.** The feed runs ahead of the node by a few blocks; the prover must not answer for a block whose state it cannot serve.
- A message with the same sequence number and a different block hash is a sequencer reorg: replace the entry and invalidate caches derived from it.
- On reconnect, request the last seen sequence number, not the next one.

**Older blocks.** A block `n` behind the signed head `N` is proven with the existing `eip2935` variant against the head: one storage proof in the history contract at block `N`, whose own `elProof` is the `arbFeedProof` of `N`. Arbitrum's history contract differs from mainnet:

| | Ethereum | Arbitrum One (ArbOS 40+) |
|---|---|---|
| Address | `0x0000F90827F1C53a10cb7A02335B175320002935` | same |
| Ring buffer | 8191 | 393168 (`0x5ffd0`), about 27 h at 250 ms |
| Index | `number % 8191` | `arbBlockNum % 393168` (L2 block number via `ArbSys.arbBlockNumber()`) |
| Readable for | `N-8191 .. N-1` | `N-393168 .. N-1` |

`c4_verify_eip2935_block` currently hard-codes 8191 in checks 5 and 6; the window must become a chain parameter. Blocks older than the window are answered with a `NotSupported` error in Milestone 1; they are the subject of Milestone 4 (section 4).

**Tags in M1.** Only `latest` and explicit block numbers. `safe` and `finalized` have L1-based meanings on Arbitrum and should be rejected (or clearly documented as unsupported) until M2.

---

## 3. Milestone 2: L1 anchor (design sketch)

When an assertion is confirmed, the rollup contract calls `Outbox.updateSendRoot(sendRoot, l2BlockHash)`, which stores

```
Outbox.roots[sendRoot] = l2BlockHash        // mapping in slot 3  [verify]
```

The L2 header carries its `sendRoot` in `extraData`, so the proof is self-contained and has the same shape as `eezL1Proof`:

```c
static const ssz_def_t ARB_L1_PROOF[] = {
    SSZ_PROG_BYTES("elHeader"),                  // RLP L2 header; extraData = sendRoot
    SSZ_CONTAINER("account", ETH_ACCOUNT_PROOF), // L1 proof of Outbox.roots[sendRoot]; account.elProof must be a clProof
};
```

Verifier: check `account.address` is the configured Outbox, the single storage key is `keccak256(extraData || pad32(3))`, the value is non-zero and equals `keccak256(elHeader)`.

Properties and limits:

- Only blocks that end an assertion are anchored directly. Blocks before an anchor are reached with `eip2935` against it. Entries in `roots` are not removed **[verify]**, so every block since ArbOS 40 has an anchor shortly after it.
- The newest anchor trails the head by the challenge period (about 6.4 days **[verify]**). State proofs against it need an archive node.
- Use: `finalized`-grade verification, historic blocks, and an independent cross-check of the sequencer (7.2).

Optional middle tier, **pending assertions**: prove `RollupCore._assertions[assertionHash]` is `Pending` with no second child, plus the assertion preimage that contains the block hash. Younger than a confirmed anchor (assertion interval **[verify]**), secured by the proposer's bond and the assumption that an honest validator would have challenged. More verifier logic (assertion hash preimage), so it is worth doing only if there is demand for a tier between "sequencer" and "a week old".

Outlook: Offchain Labs is working on ZK settlement (SP1 proofs plus committee attestation confirming assertions in hours instead of days). If adopted, the M2 anchor gets fresher without changes on colibri's side.

---

## 4. Milestone 4: historic blocks

Milestone 1 proves the head and the ring buffer behind it, about 27 hours. Everything older is a milestone of its own, described here. It builds on Milestone 1 and is independent of Milestones 2 and 3; the numbers do not fix an order.

### 4.1 Trust principle: ancestry, not old signatures

A stored checkpoint was signed by the key that was valid at the time. Trusting that signature later is wrong once the key has been rotated: the verifier only knows the current signer, and it must not accept "was a batch poster at some earlier L1 block", because a retired key that leaks afterwards could sign arbitrary old blocks.

Historic blocks are therefore never authenticated by a historic signature. They are authenticated as **ancestors of a block the current signer has signed**:

```
anchor A  (arbFeedProof, current signer)
   └─ ring buffer of A proves hash(C_k)          one EIP-2935 hop
        └─ ring buffer of C_k proves hash(C_k-1) one EIP-2935 hop
             └─ …
                  └─ ring buffer of C_j proves hash(n)
```

Every hop is the existing `eip2935` check: header, account proof of the history contract, storage proof of one slot. A block linked this way is canonical by construction, so this is stronger than the old signatures: sequencer equivocation at the time of the checkpoint no longer matters, only what the current signer's chain contains.

Two ways to deliver that chain:

| | Hop chain | ZK-compressed chain |
|---|---|---|
| Proof size | about 8 kB per hop; a year of history is roughly 320 hops, a few MB | constant: one Groth16 proof (356 bytes) plus a Merkle branch |
| Verifier work | one account and one storage proof per hop | one pairing check, one Merkle branch, one hop |
| Prover cost | none beyond stored data | one SP1 proof per re-anchoring |
| Needs | nothing new | SP1 program, colibri's existing Groth16 verifier |

The hop chain is the baseline and the fallback. The ZK variant is the same statement, compressed.

### 4.2 Server data: block hash log and checkpoints

**A. Block hash log.** A flat, append-only file with 32 bytes per L2 block, indexed by block number. It is fed from the feed, which delivers every block hash for free. Cost: 4 blocks/s, about 12.6 MB per 27 h, about 4 GB per year.

**B. Checkpoints.** On a fixed grid the server stores, for block `N`:

| Item | Size | Purpose |
|---|---|---|
| RLP header of `N` | ~0.6 kB | gives `stateRoot` |
| `eth_getProof(historyContract, [], N)` | few kB | account proof, yields `storageHash` of the ring buffer |
| `signedTail` + `signature` of `N` | message size | only needed if `N` is to serve as an anchor |

The 393168 hashes are not stored per checkpoint. They are the slice `N-393168 .. N-1` of the block hash log.

**How the hashes are obtained and why this is safe.** The storage trie of the history contract at block `N` is fully determined by those hashes:

```
for n in N-393168 .. N-1:
    key   = keccak256(pad32(n % 393168))        // secure trie key; constant table, computed once
    value = rlp(trim_leading_zeros(hash(n)))    // a hash starting with 0x00 is stored shortened
root = mpt_root(all 393168 leaves)
```

The server rebuilds the trie from the log and compares `root` with the `storageHash` of the account proof. A match proves every hash in the slice relative to the header of `N`. This is a verification, not an assumption: hashes may come from the feed, from `eth_getBlockByNumber` after a gap, or from any other source, and a wrong one cannot survive the comparison.

| Method | Verdict |
|---|---|
| Record from feed, rebuild trie, check against `storageHash` | Recommended. One small RPC call per checkpoint, nothing to fetch in a hurry. |
| `eth_getProof` with 393168 keys | Not viable. The request alone is about 27 MB, the response far larger (each storage proof is several trie nodes). This is the EEZ approach and it stops scaling well before this size. |
| `debug_storageRangeAt` | Usable as a repair path on an own node: paginated, returns hashed keys, needs the debug API and the state of `N`. |

**Operational rules**

- The feed can reorg: a repeated sequence number overwrites its log entry.
- After a disconnect the feed replays only a short backlog. Larger gaps are filled from the L2 node with `eth_getBlockByNumber`; the root check covers them.
- Run the root check more often than checkpoints are written (for example hourly against the current head) to detect a corrupt log early.
- On mismatch: refill the slice from the node, then fall back to `debug_storageRangeAt`. Never publish a checkpoint whose root does not match.
- Checkpoint distance must be at most 393168 blocks, so that `C_k` lies in the ring buffer of `C_k+1`. A fixed grid with margin (for example every 2^18 = 262144 blocks, about 18 h) makes checkpoints deterministic across prover instances and tolerates a missed one.
- On request, the trie of a checkpoint is rebuilt from the log and kept in an LRU cache; storage proofs for any slot are cut from it.
- **Backfill.** Because old checkpoints need no signature, history from before the server existed can be added later from an archive node (headers, account proofs, block hashes), back to the block where the history contract went live with ArbOS 40.

### 4.3 ZK re-anchoring

**Statement.** An SP1 program takes the hops from an anchor `A` back through all checkpoints and proves:

> Given `anchorHash`, every checkpoint `(number, hash)` committed in `checkpointsRoot` is reachable from the block with hash `anchorHash` through a chain of EIP-2935 storage proofs of the history contract.

Per hop the program performs exactly the checks of `c4_verify_eip2935_block`: `keccak256(header)` equals the hash proven by the previous hop, the account proof of the history contract verifies against `stateRoot`, the storage key is `pad32(number % 393168)`, the block lies in the window, and the stored value is the next hash. Chain id, contract address and window size are constants of the program and thus bound by its verification key.

**Public values:** `anchorNumber`, `anchorHash`, `checkpointsRoot` (Merkle root over the `(number, hash)` leaves of all covered checkpoints), `firstCheckpoint`, `count`.

**When it is built.** Whenever the trusted signer changes, the server picks the first grid checkpoint signed by the new signer as anchor `A'` and proves the history against it. To keep the work incremental the program verifies the previous proof recursively: it only walks the hops from `A'` down to the previous anchor `A`, checks that `A` is the anchor of the previous proof, and extends `checkpointsRoot`. One rotation costs one proof over the checkpoints since the last rotation, a few hundred keccak-sized MPT nodes per week of history, which is small next to a sync committee transition.

Building proofs only on rotation is sufficient, since between rotations the checkpoint signatures are by the current signer and remain valid as anchors. Building one periodically as well (for example monthly) is optional and keeps hop chains short for the most recent stretch.

**Proof format.** A new `ETH_EL_PROOF_UNION` variant authenticates a checkpoint header; the target block is then one ordinary `eip2935` hop away.

```c
// Header of a checkpoint block, proven as an ancestor of `anchor` by a ZK proof.
static const ssz_def_t ARB_ZK_CHECKPOINT_PROOF[] = {
    SSZ_PROG_BYTES("elHeader"),                // RLP header of the checkpoint C_k
    SSZ_LIST("branch", ssz_bytes32, 64),       // Merkle branch of (number, keccak256(elHeader)) in checkpointsRoot
    SSZ_BYTES32("checkpointsRoot"),            // public value of the ZK proof
    SSZ_BYTE_VECTOR("zkProof", 356),           // Groth16 proof (same format as EthZkSyncdata)
    SSZ_UNION("anchor", ETH_EL_PROOF_UNION),   // proof of the anchor block: arbFeedProof, or a cached blockHash
};
// appended to ETH_EL_PROOF_UNION as the next free index
```

Verifier:

1. Verify `anchor`. It must resolve to a block signed by the currently trusted signer (`arbFeedProof`) or to a block hash the verifier already holds. This yields `anchorHash` and `anchorNumber`.
2. Verify `zkProof` with public values `(anchorNumber, anchorHash, checkpointsRoot, …)` against the verification key configured for the chain.
3. Verify `branch`: the leaf `(number(elHeader), keccak256(elHeader))` is in `checkpointsRoot`.
4. Return `elHeader` as verified. The enclosing `eip2935` proof then proves the requested block against it.

A verifier that has seen the anchor once can cache its hash, so repeated historic requests carry only the 356-byte proof, a branch, one checkpoint header and one hop.

**Differences from the sync committee ZK proofs.** Those are anchored in the past and prove forward to a newer committee. This proof is anchored at a recent block and proves backward. The anchor therefore expires with its signer: after the next rotation the proof must be rebuilt against a new anchor, which the recursive construction makes cheap.

**Between rotation and proof.** Proving takes time. Until the new proof exists, historic requests are answered with a temporary `NotAvailable` error; rotations are rare and the gap is short. A configurable limit caps how far back proofs are built at all, which bounds the cost of a re-anchoring.

### 4.4 Relation to the L1 anchor

Milestone 2 offers a second, signer-independent route to history: `Outbox.roots` keeps the block hash of every confirmed assertion, so a block older than the challenge period can be proven with one L1 proof and one EIP-2935 hop, without ZK and with L1 as trust base. It needs the same block hash log, plus the account proof of the history contract captured for each assertion block.

Consequences for planning:

- If Milestone 2 is implemented first, the ZK chain is only needed for the span between 27 hours and the newest confirmed assertion, about a week or 10 hops. A plain hop chain covers that, and ZK re-anchoring becomes optional.
- If history is wanted before Milestone 2, or without any dependency on L1 proofs, ZK re-anchoring is the way to keep proofs small.
- Both routes can coexist: L1 for `finalized`-grade history, the ancestry chain for the recent week and as fallback.

---

## 5. Execution layer: what differs from Ethereum mainnet

### 5.1 Block header

Hashing is standard: `blockHash = keccak256(rlp(header))`. `stateRoot`, `transactionsRoot`, `receiptsRoot`, `number` and `timestamp` sit where they do on mainnet. Repurposed fields:

| Field | Arbitrum meaning |
|---|---|
| `extraData` | `sendRoot` (32 bytes), root of the L2-to-L1 message accumulator |
| `mixHash` | `be64(sendCount) \| be64(l1BlockNumber) \| be64(arbosVersion) \| flags` (byte 25 bit 0: collect tips) |
| `difficulty` | 1 |
| `nonce` | encodes `delayedMessagesRead` **[verify]** |

Other points:

- One feed message produces one block. Blocks arrive about every 250 ms under load; there are no empty slots and no fixed slot time.
- Block numbers below 22207817 are pre-Nitro ("classic") and out of scope.
- `header.timestamp` need not equal the feed message timestamp **[verify]**; the verifier takes time from the header only.

### 5.2 Transactions

Additional typed transactions exist next to 0x00, 0x01, 0x02 and 0x04. Blob transactions (0x03) are not accepted on L2.

| Type | Name | Notes |
|---|---|---|
| 0x64 | Deposit | ETH deposit from L1, no signature |
| 0x65 | Unsigned | L1-originated, sender is an aliased L1 address |
| 0x66 | Contract | L1 contract call, no nonce |
| 0x68 | Retry | redeem of a retryable ticket |
| 0x69 | SubmitRetryable | creation of a retryable ticket |
| 0x6a | Internal | ArbOS bookkeeping; the first transaction of a block **[verify]** |
| 0x78 | Legacy (classic) | pre-Nitro only |

Impact on colibri: transaction proofs must decode these types for `eth_getTransactionBy*` (hash = `keccak256(type || rlp(fields))`), and sender recovery does not apply to 0x64 to 0x6a, where the sender is a field of the transaction. The transaction trie itself is a standard MPT keyed by `rlp(index)`.

### 5.3 Receipts and logs

The receipts trie is a standard MPT. `gasUsed` includes the L1 data component. RPC-only fields such as `gasUsedForL1` and `l1BlockNumber` are not part of the consensus receipt and cannot be proven from `receiptsRoot` **[verify with test vectors, including the newer multi-gas fields]**. Log proofs work as on mainnet.

### 5.4 State and `eth_getProof`

Accounts and storage use the standard MPT with the standard account encoding, and Nitro nodes serve `eth_getProof`. ArbOS keeps its own state in the storage of a reserved account (`0xA4B05FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF`), which is provable like any other storage.

Operational note: with four blocks per second, a node's recent-state window covers little wall-clock time. The prover should take account proofs at the signed head and needs archive state for anything older.

### 5.5 EVM semantics (relevant for verified `eth_call` and simulation)

- `NUMBER` returns an approximation of the L1 block number (`l1BlockNumber` from `mixHash`), not the L2 block number. `BLOCKHASH` follows the same L1-oriented numbering and is not a real L1 hash. L2 values come from `ArbSys.arbBlockNumber()` and `arbBlockHash()`.
- `COINBASE`, `DIFFICULTY`/`PREVRANDAO` (constant 1) and `BLOBBASEFEE` differ from mainnet **[verify exact values per ArbOS version]**.
- **ArbOS precompiles** at `0x64` and up (`ArbSys` 0x64, `ArbAddressTable` 0x66, `ArbGasInfo` 0x6c, `ArbRetryableTx` 0x6e, `ArbWasm` 0x71, and more) are implemented natively in Nitro and read ArbOS storage. A call that touches one can only be verified if colibri reimplements that precompile and proves the ArbOS storage it reads. The EIP-2935 contract itself calls `ArbSys.arbBlockNumber()` (selector `0xa3b1b31d`), which only matters when the contract is executed, not when its storage is proven.
- **Stylus contracts** (WASM) cannot be executed by an EVM interpreter. They are rejected with `NotSupported` until Milestone 3 (5.7).
- **`NodeInterface` (0xc8)** is a virtual contract that exists only in the RPC layer. Its results (gas estimation components, L1 fee estimates) have no state and cannot be proven.
- **Gas** is two-dimensional: L2 execution plus an L1 data fee charged in L2 gas units. `eth_estimateGas` and exact gas accounting in simulation need the ArbOS pricing state. The base fee is set by ArbOS, not by EIP-1559's formula, and priority fees are normally not paid out.
- **Hard forks** are ArbOS versions (in `mixHash`), activated by on-chain governance, not by timestamp tables. The EVM rule set must be chosen from the ArbOS version of the header.

### 5.6 RPC tags

`latest` is the sequencer head, `safe` the newest block whose batch is on L1, `finalized` the same at a finalized L1 block. These are node-local derivations from L1 data and have no header field.

### 5.7 Stylus (WASM) contracts

colibri executes `eth_call` with evmone on storage values embedded in the proof. For EVM contracts on Arbitrum this works as on mainnet, with the host adjustments from 4.5. Stylus contracts share the same state (account, code hash, storage trie, ABI, logs) but are executed by a different engine, which evmone does not provide.

How Stylus works, as far as the verifier is concerned:

| Aspect | Detail |
|---|---|
| Code in state | discriminant `0xEFF000…` followed by brotli-compressed WASM. Newer ArbOS versions add fragment and root variants of the prefix for programs split over several accounts. |
| Limits | 128 kB decompressed WASM, 128 memory pages (8 MB), 4-page stack (initial parameters, changeable by governance) |
| Activation | a program must be activated through `ArbWasm` before use. Nitro then instruments the WASM (ink metering, stack depth, heap bounds). Activation data is stored in ArbOS state and expires after 365 days. |
| Gas | metered in ink; 1 gas buys 10000 ink (initial parameter) |
| Dispatch | a call whose target code carries the Stylus prefix is routed to the WASM runtime instead of the EVM interpreter |
| Host functions | the WASM imports host functions for storage, calls, logs, context and keccak. Storage access ends in the same storage trie as `SLOAD`/`SSTORE`. |
| Interop | EVM and Stylus contracts call each other with the normal ABI, in both directions |

Consequences:

- The **data side of the proof does not change.** Code proofs and storage proofs for a Stylus contract are ordinary account and storage proofs.
- The **execution side is missing.** An EVM contract can call into Stylus code at any depth, so the check cannot be made on the entry point alone.

**Milestones 1 and 2: `NotSupported`.** The host inspects the code of every call target (`CALL`, `STATICCALL`, `DELEGATECALL`, `CALLCODE`, and the top-level `to`). If it starts with a Stylus discriminant, verification stops with a dedicated `NotSupported` error that names the contract address. The prover runs the same check while it collects the proof and returns the error before building one, so the client learns it without downloading anything. No partial or unverified result is ever returned.

**Milestone 3: full Stylus support.** See section 8.

---

## 6. Comparison with existing colibri L2 paths

| | OP-Stack (`sequencerProof`) | EEZ (`eezL1Proof`) | Arbitrum M1 (`arbFeedProof`) | Arbitrum M2 (`arbL1Proof`) |
|---|---|---|---|---|
| Signed or anchored object | execution payload | block hash in L1 storage | feed message including block hash | block hash in L1 storage |
| Signer discovery | config | n/a | sync state, proven on L1 via `sync_data` | n/a |
| Data source | p2p gossip | L1 + L2 RPC | sequencer feed (remote prover) | L1 + L2 RPC |
| Proof size | one payload | one header + L1 proof | one header + one message | one header + L1 proof |
| Lag | none | minutes to hours | none | about a week |

---

## 7. Security considerations and risks

### 7.1 Trust model of M1

The verifier trusts one key held by the sequencer operator. That is the trust every Arbitrum RPC user already places in the chain's soft finality, minus the trust in the RPC provider: a provider can withhold, but cannot forge. It is weaker than mainnet verification (sync committee) and weaker than M2.

### 7.2 Sequencer equivocation and reorgs

The sequencer can sign two different blocks for one sequence number, by fault, failover or malice, and both signatures verify. Nothing on the feed makes this slashable. Until the batch is on L1 and the assertion confirmed, an M1-verified block is a promise. Mitigations: document the finality level, expose it in the API, let applications that move value wait for M2-grade proofs or compare the M1 chain against the latest L1 anchor (the anchored block must be an ancestor, checkable through `eip2935`).

### 7.3 Key compromise and rotation

The feed key is the batch poster's hot wallet, an online key that also signs L1 transactions. If it leaks, an attacker can sign arbitrary block hashes until the address is removed from `isBatchPoster`. A verifier does not see the removal by itself, and an attacker holding the removed key can keep serving it old-key signatures, which is most dangerous for verifiers that were offline for a long time. The signer validity window in 2.4 bounds this: a signer is trusted for at most `W` (default 27 h) past the timestamp of its last L1 proof, and a replayed proof cannot extend that. A legitimate rotation without the L1 path would break all deployed verifiers, which is the second reason to implement it in M1 and not later. Historic blocks are never authenticated by an old signature but as ancestors of a block signed by the current signer (4.1).

### 7.4 Messages without block hash, and field ambiguity

The digest is a flat concatenation with two variable-length fields and an optional `blockHash`. A third party cannot forge signatures but could try to re-interpret a genuine one, in particular a message signed without `blockHash`, where the bytes after `sequenceNumber` are `blockMetadata | delayedMessagesRead | …`. The verifier always places `keccak256(elHeader)` at that position. For the digests to match, the attacker needs a header whose hash equals 32 bytes of structured message data, a keccak preimage. The construction is safe under preimage resistance; no extra check is needed. The chain id and prefix prevent replay across chains and across other uses of the key (L1 transactions sign RLP with a different structure).

### 7.5 Stale `latest`

A prover can serve an old but correctly signed block as `latest`; signatures do not expire. colibri's existing gate covers this: `eth_check_latest_freshness` rejects a `latest` proof whose header timestamp is below `ctx->min_latest_block_ts`. It works for Arbitrum unchanged, since the timestamp comes from the verified header. With blocks every 250 ms the host can choose a much tighter bound than on mainnet. The gate is off when the host passes 0; the same value serves as the clock for the signer validity window (2.4).

### 7.6 Availability

The feed is a single service run by the operator. If it is down, lagging or rate-limited, M1 proofs stop while the chain may continue. Fallbacks in order: other feed relays, `witnessProof` once implemented, M2. Liveness also depends on the prover's L2 node keeping up with the feed.

### 7.7 Format stability

`signatureV2` replaced an earlier `signature` field with a different preimage. Nitro's source warns that changing `SignatureHash` breaks older clients, so changes are unlikely to be silent, but a future V3 is possible. The opaque tail tolerates appended fields; a change before `blockHash` or to the prefix needs a new proof variant. The prover should alert when recovery stops yielding the known signer.

### 7.8 Trust base of M2 and of the signer proof

Both rely on upgradeable L1 contracts. Arbitrum One is a Stage 1 rollup: the Security Council can upgrade contracts, replace batch posters and, in an emergency, act without delay. Contract addresses and storage slots are part of colibri's chain config and must be re-validated after Arbitrum governance upgrades. The M2 anchor additionally assumes at least one honest, live validator during the challenge period.

### 7.9 EIP-2935 chain

Blocks reached through the history contract inherit the trust of the block they are proven against. An M1 head therefore puts up to 27 hours of history under sequencer trust, even though most of it may already be posted to L1. Historic proofs of Milestone 4 extend this: all history hangs on one anchor signed by the current signer, plus the soundness of the ZK proof where one is used.

### 7.10 Proof size and DoS

Message size is bounded by Nitro's limits, but the SSZ bound of `ETH_SEQUENCER_PAYLOAD_UNION` is 1 GiB. Embedded verifiers should enforce their own cap and bound the ZSTD output size before decompressing.

### 7.11 Execution-layer gaps

Rejecting what cannot be verified is safer than approximating it: calls into Stylus contracts, calls touching unimplemented ArbOS precompiles and `NodeInterface` results fail closed with `NotSupported` instead of returning unverified data. The Stylus check sits on every call target inside the host, because an EVM contract can reach Stylus code at any call depth (5.7).

---

## 8. Implementation plan

**Milestone 1**

1. Prover: feed client, signature check, in-memory head, `latest` resolution (2.5).
2. Verifier: `arbFeedProof` variant, chain config (chain id, genesis block, `SequencerInbox`, signer validity window), signer with L1 proof time in the sync state, `ArbSignerSyncdata` in `sync_data`, expiry check against the clock (2.4).
3. `eip2935`: history window as a chain parameter (393168).
4. Transaction types 0x64 to 0x6a in transaction and receipt proofs.
5. `eth_call`: `NUMBER`/`BLOCKHASH` semantics, `NotSupported` for Stylus call targets (prover and verifier) and for unimplemented ArbOS precompiles; then the most used precompiles (`ArbSys`, `ArbGasInfo`).
6. Test vectors from recorded feed messages; cross-check block hashes against an L2 RPC.

**Milestone 2**

1. `arbL1Proof` against `Outbox.roots`.
2. `safe` / `finalized` tag support based on L1.
3. Cross-check of M1 heads against the latest anchor.
4. Optional: pending assertions.

**Milestone 3: Stylus support**

Goal: verified `eth_call` and simulation for calls that enter Stylus contracts, with the same result and gas behaviour as Nitro.

1. **Measure first.** Count how many real calls on Arbitrum One reach Stylus code (from the prover's `NotSupported` errors). This sets the priority of the milestone.
2. **Second engine behind EVMC.** evmone is an EVMC VM. A Stylus runtime is added as another EVMC VM next to it; the host selects the engine by code prefix. Nested calls in both directions then work through the existing host interface.
3. **Program loading.** Code proof, prefix handling including fragment and root variants, brotli decompression with the Stylus dictionary, validation against the limits.
4. **WASM execution.** A deterministic interpreter that fits colibri's targets (candidates: wasm3, wasmi, WAMR). Stylus restricts WASM features to keep execution deterministic; the exact feature set is taken from Nitro's validation rules.
5. **Host functions.** Map the Stylus imports (storage load/cache/flush, contract calls, logs, account and block context, keccak, memory growth) onto the EVMC host calls colibri already implements.
6. **Ink metering.** Reproduce Nitro's instrumentation so that out-of-gas, gas introspection and gas forwarding behave identically. Without it a result is only right while gas is plentiful and the program does not look at its gas; that is acceptable for an internal preview, not for a release.
7. **ArbOS state.** Prove and apply the Stylus parameters (version, ink price, limits) and the activation record of each program. An expired or never-activated program must revert as it does on chain.
8. **Prover.** Make sure the access collection also captures storage read through Stylus host functions and by ArbOS precompiles, which do not pass through `SLOAD`.
9. **Conformance.** Differential tests against a Nitro node (`eth_call` and `debug_traceCall`) over recorded mainnet calls, covering EVM-to-Stylus, Stylus-to-EVM and reverts.
10. **Targets.** Enable per build: browser/WASM and server first. Embedded targets keep `NotSupported`, since up to 8 MB of linear memory per program does not fit them.

Risks specific to this milestone: bit-exact metering is the largest piece of work; Stylus versions change the instrumentation and require re-activation, so the runtime must track ArbOS upgrades; a WASM runtime and brotli add code size and attack surface to the verifier.

**Milestone 4: historic blocks**

1. Server: block hash log fed from the feed, gap filling, periodic root check against `storageHash` (4.2).
2. Server: checkpoint grid, storage of header and account proof, trie rebuild with LRU cache.
3. Prover: hop-chain proofs for blocks older than the ring buffer, with a configurable maximum depth.
4. Decide between ZK re-anchoring and the L1 route (4.4), depending on whether Milestone 2 is in place.
5. ZK: SP1 program for the EIP-2935 chain with recursive extension, `arbZkCheckpointProof` variant, verifier key in the chain config (4.3).
6. Server: re-anchoring job triggered by a signer change; fallback behaviour while the proof is being built.
7. Backfill of checkpoints from an archive node, back to ArbOS 40.

---

## 9. Open items to verify

- Deployed storage layout of `SequencerInbox` (slots 3 and 9) and `Outbox` (slot 3) behind their proxies.
- Outbox and rollup addresses for Arbitrum One; challenge period and assertion interval.
- `roots` entries are never deleted.
- `nonce` = `delayedMessagesRead`; relation between message timestamp and header timestamp.
- First transaction of every block is type 0x6a.
- Consensus receipt encoding, including multi-gas fields, against real blocks.
- Whether every Arbitrum One feed message carries `blockHash` over a long capture (20 of 20 so far), and how relays (third-party feeds) forward `signatureV2`.
- Maximum feed message size, for the verifier's proof size cap.
- Ring buffer completeness: all 393168 slots are populated and the contract holds no other storage, so the rebuilt root can match.
- Time to rebuild the 393168-leaf trie with colibri's MPT builder, and its memory footprint.
- State retention of the L2 node: how long after block `N` the account proof of the history contract can still be fetched.
- Format of `client_state` for the signer and its L1 block number.
- Activation block of ArbOS 40 on Arbitrum One, the lower bound for EIP-2935 based history.
- SP1 cycle count and cost of the EIP-2935 chain program per hop, and of the recursive step.
- Size of one hop (account proof plus storage proof of the history contract) on real data.
- Opcode differences per ArbOS version.
- Stylus: exact code prefixes per ArbOS version, the permitted WASM feature set, the list of host functions, and where activation records and parameters live in ArbOS state.
- Whether `eth_createAccessList` reports storage read by Stylus host functions and ArbOS precompiles.

---

## 10. Sources

- Nitro, feed message and signature hash: https://github.com/OffchainLabs/nitro/blob/master/broadcaster/message/message.go
- Nitro, signature test vector: https://github.com/OffchainLabs/nitro/blob/master/broadcaster/message/message_signature_test.go
- Nitro, signer verification: https://github.com/OffchainLabs/nitro/blob/master/util/signature/verifier.go
- Nitro, feed signer wallet: https://github.com/OffchainLabs/nitro/blob/master/cmd/nitro/nitro.go
- Nitro contracts, `SequencerInbox`: https://github.com/OffchainLabs/nitro-contracts/blob/main/src/bridge/SequencerInbox.sol
- Nitro contracts, `AbsOutbox`: https://github.com/OffchainLabs/nitro-contracts/blob/main/src/bridge/AbsOutbox.sol
- Arbitrum go-ethereum fork, header info and transaction types: https://github.com/OffchainLabs/go-ethereum/blob/master/core/types/arb_types.go
- Arbitrum go-ethereum fork, Stylus code prefixes: https://github.com/OffchainLabs/go-ethereum/blob/master/core/state/statedb_arbitrum.go
- Nitro, Stylus parameters: https://github.com/OffchainLabs/nitro/blob/master/arbos/programs/params.go
- Arbitrum go-ethereum fork, EIP-2935 parameters: https://github.com/OffchainLabs/go-ethereum/blob/master/params/protocol_params.go
- Arbitrum docs, reading the sequencer feed: https://docs.arbitrum.io/run-arbitrum-node/sequencer/read-sequencer-feed
- Arbitrum docs, verifying L2 state on L1: https://docs.arbitrum.io/arbitrum-essentials/how-to-get-l2block-on-l1
- L2BEAT, BoLD proof system walkthrough: https://docs.l2beat.com/code_walkthroughs/arbitrum/proof_system.html
- ArbOS 40 release notes: https://docs.arbitrum.io/run-arbitrum-node/arbos-releases/arbos40
- Arbitrum blog, ZK settlement: https://blog.arbitrum.io/zk-settlement-is-coming
- Batch submitter on Etherscan: https://etherscan.io/address/0xC1b634853Cb333D3aD8663715b08f41A3Aec47cc
- colibri proof types: https://github.com/corpus-core/colibri-stateless/blob/dev/src/chains/eth/ssz/verify_proof_types.h