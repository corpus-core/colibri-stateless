: Arbitrum

Colibri proves an Arbitrum One block hash from the sequencer feed, then reuses the Ethereum verifier for everything that hangs off that hash. The module is built with the cmake-option `CHAIN_ARB`.

The sequencer signs each L2 block before the data reaches Ethereum. The verifier checks that signature against the batch poster last proven in the `SequencerInbox` on L1. The L1 proof itself is a sync-committee proof of Ethereum mainnet, the same state the verifier already keeps for mainnet.

What that covers today:

| | Sequencer feed | L1 anchor | Stylus | Historic blocks |
|---|---|---|---|---|
| Status | implemented | not implemented | not implemented | not implemented |
| Trust | batch poster key, proven on L1 | Ethereum sync committee and the rollup contracts | same as the block it runs in | ancestry of a feed-signed block |
| How far | `latest`, and blocks inside the last 393168 L2 blocks (about 27 h) | confirmed assertions | execution of WASM contracts | blocks older than the history window |
| Proof | `arbFeedProof` (index 6), or one `eip2935` hop under it | — | — | — |

`safe` and `finalized` are rejected. On Arbitrum those tags are derived from L1, and this build has no L1 anchor.

## Background

Arbitrum One has one sequencer. Each sequenced message becomes one L2 block and is published on a WebSocket feed before the batch poster submits the compressed batch to the `SequencerInbox` on L1. Validators later assert the resulting state. A confirmed assertion is the L1 anchor described on the next pages; it is not what this build verifies.

The feed message carries the L2 block hash and an ECDSA signature over it. That signature is the block proof. The batches on L1 commit to inputs, not to a block hash, so a stateless verifier cannot recover the hash from them without re-executing the chain.

## Where Ethereum takes over

`c4_arb_verify` applies an `arbSignerSyncData` update when the request carries one, then calls `c4_eth_verify_request`. Account, storage, transaction, receipt, log and call proofs are the Ethereum proofs. Their `elProof` is an `arbFeedProof`, an `eip2935` proof whose newer block is an `arbFeedProof`, or a `blockHash` the verifier cached after one of those checks.

The request container is the Ethereum `C4Request`. Method classification uses the Ethereum catalog. `latest` still passes `eth_check_latest_freshness`: a host that sets `min_latest_block_ts` rejects a `latest` header older than that timestamp. Arbitrum is not exempt from that gate.

:: Sequencer feed

The feed proof is `arbFeedProof`, index 6 of `ETH_EL_PROOF_UNION`. Existing indices stay frozen.

## What the sequencer signs

Nitro's `BroadcastFeedMessage.SignatureHash` (`broadcaster/message/message.go`) hashes a flat concatenation. There is no length prefix and no RLP. The digest is

```text
keccak256( "Arbitrum Nitro Feed:" | be64(chainId) | be64(sequence) | blockHash | signedTail )
```

`chainId` is the numeric id. For Arbitrum One that is 42161. `sequence` is the L2 block number minus the Nitro genesis, 22207817. `blockHash` is `keccak256` of the RLP execution header. A message signed without a block hash cannot verify, because the verifier always inserts that hash.

`signedTail` is everything after `blockHash` in Nitro's preimage:

| # | Field | Size | Encoding |
|---|---|---|---|
| 1 | `blockMetadata` | variable | raw bytes |
| 2 | `delayedMessagesRead` | 8 | uint64 big-endian |
| 3 | `kind` | 1 | uint8 |
| 4 | poster | 20 | raw address (`header.sender`) |
| 5 | L1 block number | 8 | uint64 big-endian (`header.blockNumber`) |
| 6 | timestamp | 8 | uint64 big-endian (`header.timestamp`) |
| 7 | `requestId` | 32 or 0 | raw, omitted when null |
| 8 | `baseFeeL1` | 0..32 | big-endian, no leading zeros; omitted when null or zero |
| 9 | `l2Msg` | variable | raw bytes |

The fixed fields 2 to 6 are 45 bytes. A shorter tail is rejected. The signature is 65 bytes `r || s || v`, secp256k1 over the digest, with no EIP-191 prefix. `v` may be 0/1 or 27/28.

The verifier does not parse the tail. It hashes the bytes. The prover rebuilds those bytes from the feed JSON, so a field Nitro appends after `blockHash` stays inside the tail.

## Proof format

```c
static const ssz_def_t ARB_FEED_PROOF[] = {
    SSZ_PROG_BYTES("elHeader"),                           // RLP L2 execution header
    SSZ_UNION("signedTail", ETH_SEQUENCER_PAYLOAD_UNION), // index 0 zstd, index 1 raw
    SSZ_BYTE_VECTOR("signature", 65),
};
```

`elHeader` is the header whose hash was signed. `blockHash` and the sequence number are derived from it; they are not fields of the proof. `signedTail` reuses the OP-Stack payload union. The server stores the uncompressed member (index 1). The verifier also accepts zstd, and only when the frame carries its content size. The uncompressed length must be at least 45 bytes and at most 32 MiB, whether it arrived compressed or not.

The proof has no signer address. Who may sign is the cached batch poster, installed by `arbSignerSyncData` before this check.

## Verification

`arb_verify_feed_proof` runs only for `C4_CHAIN_TYPE_ARBITRUM`.

1. `elHeader` must be an execution header. Its block number must be at least 22207817. That genesis is a constant of the verifier, used for every chain of this type.
2. `blockHash = keccak256(elHeader)`, `sequence = number - 22207817`.
3. Decompress `signedTail` when it is the zstd member. Reject a frame with an unknown content size, a size of 0, or a size above 32 MiB.
4. `digest` is the concatenation above, with `ctx->chain_id` as the chain id.
5. Recover the signer. Accept the header when that address equals the cached batch poster for this chain.

A missing cache is an error (`unknown batch poster`). The recovered address is compared with the cache and with nothing else: the check does not read the clock, and it does not read `min_latest_block_ts`. With `EL_HEADER_CACHE` the verified header is stored, so a later proof of the same block can use the `blockHash` variant.

## Batch poster

The feed is signed by the batch poster's L1 key. On Arbitrum One that address is an entry in `SequencerInbox.isBatchPoster` at `0x1c479675ad559DC151F6Ec7ed3FbF8ceE79582B6`. The mapping slot is 3. The storage key is `keccak256(pad32(signer) || pad32(3))`. For `0xC1b634853Cb333D3aD8663715b08f41A3Aec47cc` the key is `0xa10aa54071443520884ed767b0684edf43acec528b7da83ab38ce60126562660`. The proven value must be 1.

The verifier stores one record per chain, under `batch_poster_<chain_id>`: the 20-byte address, the Unix time in milliseconds at which the proof was accepted, and the L1 block number of that proof. The time is the verifier's clock at acceptance, not the timestamp of the L1 header.

**Sync data.** Index 3 of `C4_ETH_REQUEST_SYNCDATA_UNION`:

```c
static const ssz_def_t ARB_SIGNER_SYNCDATA[] = {
    SSZ_ADDRESS("signer"),
    SSZ_CONTAINER("account", ETH_ACCOUNT_PROOF),
    SSZ_UNION("sync_data", C4_ETH_REQUEST_SYNCDATA_UNION), // L1 sync: none, light client, or ZK
};
```

`account.elProof` must be a `clProof`. A cached `blockHash` is rejected, so the L1 header is checked by the sync committee. The nested `sync_data` is the L1 prover's own sync update and is applied first, on the L1 chain. For Arbitrum One that chain is Ethereum mainnet. A nested `arbSignerSyncData` is rejected.

Then:

1. `account.address` is the `SequencerInbox` configured for the chain.
2. The L1 header has a state root and a non-zero block number.
3. That L1 number is greater than or equal to the number already stored. A smaller number is a replay and is rejected. The same number is accepted and refreshes the stored time.
4. The single storage proof is for the key recomputed from `signer`, and the value is 1.

The record is written after those checks. A pending L1 update leaves the outer sync object in place, so the next round continues on the same path.

**How the client asks for it.** The address is not part of the `c4` client state. `c4` stays the L1 sync snapshot. While the stored acceptance time is at most 24 hours old (`C4_BATCH_POSTER_MAX_AGE_MS`), a prover request for this chain includes `"batch_poster":"0x…"`. The server compares that value with the signer of the current feed head. A missing or different address makes the prover build `arbSignerSyncData`. The flag that triggers it is internal; a client cannot set it.

The L1 request is `eth_getStorageAt` of the inbox slot at `latest`, sent to an L1 prover with the client's `c4` snapshot. It does not forward `batch_poster` and does not send `last_block_hash`, so the returned block stays a `clProof`. The response's account proof and its `sync_data` become the `arbSignerSyncData` container.

A verifier with an empty cache, and a client whose record is older than 24 hours, therefore receive a fresh L1 proof from a prover that follows this path. A response that simply omits `arbSignerSyncData` leaves the cached address in place, and feed proofs signed by it keep verifying.

Further rollups can be registered at runtime with a chain id and a `SequencerInbox` address (at most eight, and not over a built-in chain). The digest still subtracts 22207817, and the L1 of the account proof is mainnet only for Arbitrum One, unless an L1 is registered for that chain.

## Blocks inside the history window

`latest`, the block number of the signed head, and the hash of that head are the feed proof itself.

An older number or hash is one `eip2935` proof. The newer block is the feed proof of the head. The history contract is `0x0000F90827F1C53a10cb7A02335B175320002935`, the same address as on Ethereum. For every chain of type Arbitrum the window is 393168 (`ARB_EIP2935_HISTORY_WINDOW`) and the slot is `pad32(number % 393168)`. The verifier checks that slot, checks `newer - 393168 <= number < newer`, and checks that the stored value equals `keccak256(elHeader)`.

The prover builds that single hop. A block newer than the head, a block outside the window, an empty history slot, or a hash that is not the stored one is an error. `safe`, `finalized` and any other tag are an error: the prover accepts `latest`, a block number, or a block hash.

The verifier allows an `eip2935` proof whose newer block is itself an `eip2935` proof, bounded by the SSZ nesting limit. This prover does not build that chain. Blocks older than one window are the historic-blocks design.

## Prover and feed

The feed is a stream. The server keeps one WebSocket, on the libuv loop, to `arb_feed_url` (environment `ARB_FEED_URL`, default `wss://arb1-feed.arbitrum.io/feed`). The upgrade sends `Arbitrum-Feed-Client-Version: 2`. After a disconnect that has already seen a sequence, it also sends `Arbitrum-Requested-Sequence-Number` with that sequence. Permessage-deflate is not offered; a compressed frame closes the connection. Reconnects back off from 1 s to 30 s. Thirty seconds without a frame also reconnects.

A message is kept when it has a `blockHash`, a 65-byte `signatureV2`, and a tail of at least 45 bytes. The newest such message replaces the head, including a message with a lower sequence number. Nothing is persisted, and no earlier message is retained. The signer is recovered when a proof needs it, from the chain id, the sequence, the hash, the tail and the signature. There is no allow-list at ingest.

`arb_preconf/latest` returns the `arbFeedProof` container. The header is read from the L2 node: `debug_getRawHeader` when a configured server advertises that method, otherwise `eth_getBlockByHash`. The header is kept only when its keccak equals the feed hash. The tail is stored uncompressed. The node answer is not cached. A `null` result is retried once, after 250 ms, because the feed runs ahead of the node; a second miss fails the request. The prover does not step back to an older signed message.

A proof that needs transactions loads `debug_getRawBlock` for the proven hash. A block encoded as header, transactions and uncles is stored with an empty withdrawals list. The header inside that encoding must hash to the proven block hash.

## Trust

The verifier trusts one key, the batch poster last installed from L1, and the Ethereum sync committee that signed the L1 block of that proof. That is the trust of Arbitrum soft finality. It is weaker than a sync-committee proof of the L2 block, and weaker than a confirmed assertion. The sequencer can sign two blocks for one sequence number. Both signatures verify. The server keeps whichever message arrived last.

A removed key stays acceptable until a newer L1 proof overwrites the cache. The 24 hour figure decides when the client stops advertising the cached address, which is when a prover that implements this path attaches a new proof. It is not a deadline inside `arb_verify_feed_proof`.

The feed is one service. While it is down, this proof stops. The L2 node must also have imported the signed head; otherwise `arb_preconf/latest` fails after the one retry.

The digest binds the chain id and the header hash. A signature over some other concatenation would need a header whose keccak equals 32 bytes of that concatenation. The 32 MiB cap is the verifier's own limit; the SSZ bound of the shared payload union is 1 GiB.

:: Execution layer

Once the header is accepted, the Ethereum verifier reads `stateRoot`, `transactionsRoot` and `receiptsRoot` as on mainnet. The differences below are the ones this build actually applies. Anything else follows the Ethereum path.

## Block header

The block hash is `keccak256` of the RLP header. For an Arbitrum chain the block JSON additionally exposes:

| JSON field | Source |
|---|---|
| `l1BlockNumber` | `mixHash` bytes 8..16, big-endian, when `mixHash` is at least 16 bytes |
| `sendCount` | `mixHash` bytes 0..8, big-endian, on the same condition |
| `sendRoot` | `extraData`, when it is 32 bytes |
| `difficulty` | the header's difficulty field, as a uint64 |

A header with fewer than the post-Shanghai fields omits the corresponding JSON keys, by the same field count the Ethereum encoder uses. The verifier does not interpret `nonce`, and it does not read an ArbOS version out of `mixHash`.

## Transactions

The transaction trie is the Ethereum MPT, keyed by `rlp(index)`. The hash of a typed transaction is `keccak256(type || rlp(fields))`.

Alongside types 0x00, 0x01, 0x02 and 0x04, the decoder accepts the six Nitro types. There is no signature on them. `v`, `r` and `s` in the RPC object are zero. The sender of an internal transaction (`0x6A`) is the ArbOS address `0x00000000000000000000000000000000000a4b05`. Every other Nitro type carries `from` in the RLP. The RPC `to` of an internal transaction is that same ArbOS address. The RPC `to` of a submit-retryable (`0x69`) is the retryable precompile `0x6e`. The other types use the RLP address, which is null when the field is nil.

| Type | Name | Extra RPC fields |
|---|---|---|
| `0x64` | deposit | `requestId` |
| `0x65` | unsigned | — |
| `0x66` | contract | `requestId` |
| `0x68` | retry | `ticketId`, `maxRefund`, `submissionFeeRefund`, `refundTo` |
| `0x69` | submit-retryable | `requestId`, `refundTo`, `l1BaseFee`, `depositValue`, `retryTo`, `retryValue`, `retryData`, `beneficiary`, `maxSubmissionFee` |
| `0x6A` | internal | — |

`maxFeePerGas` is set for `0x66`, `0x68` and `0x69`. A type the decoder does not know, including the pre-Nitro type `0x78`, fails the proof.

## Receipts and logs

The receipts trie is the Ethereum MPT. On an Arbitrum chain the effective gas price written into the receipt object is the header base fee, for every transaction type. `l1BlockNumber` on the receipt is the same `mixHash` value as on the block, not a field of the consensus receipt. Log proofs are unchanged.

## State

Accounts and storage use the standard account encoding and the standard MPT. `eth_getProof` is verified like on mainnet. The history-contract proof above is that same check, with the Arbitrum window.

## Calls

`eth_call` and the other execution methods run in the Ethereum host, against the state embedded in the proof. The block number that host reports is the L2 block number from the header. The host has no Stylus runtime and no ArbOS precompiles. A call that enters either still runs on that host and returns its result. Closing that gap is the Stylus page and the call-semantics work that is not in this build.

:: L1 anchor

Not implemented. This page is the design for a proof of a confirmed assertion. It does not describe the verifier that is built today.

When an assertion is confirmed, the rollup contract calls `Outbox.updateSendRoot(sendRoot, l2BlockHash)`. The L2 header carries that `sendRoot` in `extraData`, so a proof can follow the shape of `eezL1Proof`:

```c
static const ssz_def_t ARB_L1_PROOF[] = {
    SSZ_PROG_BYTES("elHeader"),                  // RLP L2 header; extraData = sendRoot
    SSZ_CONTAINER("account", ETH_ACCOUNT_PROOF), // L1 proof of Outbox.roots[sendRoot]; elProof is a clProof
};
```

The verifier would check that `account.address` is the configured Outbox, that the single storage key is `keccak256(extraData || pad32(slot))`, and that the value equals `keccak256(elHeader)`. The slot of `roots` is part of the chain config and has to be taken from the deployed proxy. Only the block that ends an assertion is anchored directly. Earlier blocks are an `eip2935` hop against that anchor, inside the 393168 window of this build, or the historic chain below.

The newest confirmed assertion trails the head by the challenge period. Proofs against it need state at that block. The tags `safe` and `finalized` belong to this proof, because on Arbitrum they are defined from L1.

A pending assertion would be a separate tier: `RollupCore._assertions[assertionHash]` is `Pending`, the assertion has no second child, and the preimage contains the block hash. It is younger than a confirmation and rests on the proposer's bond. It needs its own preimage checks, so it is worth building only if something needs a tier between the feed and a confirmed assertion.

Offchain Labs has described ZK settlement that would confirm assertions in hours. If that lands in the same storage slot, this proof gets fresher without a new variant.

## Open questions

- Storage slot of `Outbox.roots` behind the proxy, and whether entries are removed.
- Outbox and rollup addresses, the challenge period, and the assertion interval.
- Whether every confirmed assertion since ArbOS 40 is still readable.

:: Historic blocks

Not implemented. The prover answers a block older than 393168 behind the signed head with an error. This page is how those blocks would be proven later. It depends on the sequencer feed and does not depend on the L1 anchor or on Stylus.

## Ancestry

A checkpoint was signed by whatever key was valid then. After a rotation the verifier only knows the current batch poster, and a proof that some address used to be a batch poster would accept a retired key. Historic blocks are therefore ancestors of a block the current signer has signed:

```text
anchor A          arbFeedProof, current signer
  ring of A       one eip2935 hop, proves hash(C_k)
    ring of C_k   one eip2935 hop, proves hash(C_k-1)
      …
        ring of C_j proves hash(n)
```

Each hop is the `eip2935` check that already exists: header, account proof of the history contract, one storage proof. The window per hop is 393168. A block linked this way is whatever the current signer's chain contains.

| | Hop chain | ZK-compressed chain |
|---|---|---|
| Proof size | one account proof and one storage proof per hop | one Groth16 proof (356 bytes) plus a Merkle branch |
| Verifier work | those two proofs per hop | one pairing check, one branch, one hop |
| Needs | stored headers and proofs | an SP1 program and the existing Groth16 verifier |

The hop chain is the baseline. The ZK proof is the same statement, compressed.

## What the server would store

**Block hash log.** 32 bytes per L2 block, indexed by block number, fed from the feed. A repeated sequence overwrites its entry. After a gap, hashes can be filled with `eth_getBlockByNumber`. About 4 blocks/s is about 4 GB per year.

**Checkpoints.** On a grid of at most 393168 blocks, for block `N`: the RLP header, the account proof of the history contract at `N`, and the feed signature of `N` when `N` is allowed to be an anchor. The 393168 hashes are the slice of the log, not a copy per checkpoint.

The storage root of the history contract at `N` is determined by that slice:

```text
for n in N-393168 .. N-1:
    key   = keccak256(pad32(n % 393168))
    value = rlp(trim_leading_zeros(hash(n)))
root = mpt_root(all 393168 leaves)
```

Rebuilding the trie and comparing `root` with the `storageHash` of the account proof checks the slice. A mismatch is not published. `eth_getProof` of all 393168 keys does not scale; `debug_storageRangeAt` is a repair path on a node that still has the state and the debug API.

The grid has to keep `C_k` inside the window of `C_k+1`. A fixed spacing with margin (for example 262144) survives a missed checkpoint. On a request the trie is rebuilt from the log and cached. History from before the server started can be backfilled from an archive node, because an old checkpoint is not authenticated by its own signature. The lower bound is the block where the history contract went live with ArbOS 40.

## ZK re-anchoring

An SP1 program would take the hops from an anchor and prove: every checkpoint committed in `checkpointsRoot` is reachable from `anchorHash` by `eip2935` proofs of the history contract. Chain id, contract address and window are constants of the program.

Public values: `anchorNumber`, `anchorHash`, `checkpointsRoot`, `firstCheckpoint`, `count`. The program can verify the previous proof and only walk the hops since the previous anchor, so a rotation costs one proof over the checkpoints since the last one.

The proof variant would authenticate a checkpoint header. The requested block is then one ordinary `eip2935` hop from that header. The anchor is an `arbFeedProof`, or a cached hash of a block the verifier already holds. After the next rotation the anchor expires with its signer and the proof is rebuilt.

Until that proof exists, historic requests would fail. A configurable limit would cap how far back proofs are built.

## Relation to the L1 anchor

A confirmed assertion stores the hash of the block that ends it. With the L1 anchor, a block older than the challenge period is one L1 proof plus one `eip2935` hop, and the ZK chain is only needed for the week between the history window and the newest confirmation. Without the L1 anchor, the ancestry chain is the whole history. Both can exist: L1 for finalized history, the feed anchor for the recent window.

:: Stylus

Not implemented. `eth_call` uses the Ethereum host for every contract.

Stylus contracts share account, code hash, storage and logs with EVM contracts. Their code starts with a Stylus discriminant (`0xEFF000…` in the versions described by Nitro, with fragment and root prefixes when a program is split). The EVM host does not execute that code. Nitro runs it in a WASM engine after activation through `ArbWasm`, with ink metering. EVM and Stylus call each other through the normal ABI, so the engine cannot be chosen from the top-level `to` alone.

A Stylus build would add a second EVMC VM, selected by the code prefix, and would have to reproduce Nitro's instrumentation, the host imports, and the activation record in ArbOS state. Code and storage proofs stay ordinary account and storage proofs. The prover would have to record storage that Stylus host functions read, which does not pass through `SLOAD`.

The limits Nitro starts from are 128 kB of decompressed WASM, 128 memory pages, and 10000 ink per gas. Activation expires. Those parameters live in ArbOS state and move with governance, so a runtime has to take them from the proven state of the block.

Embedded targets stay on the Ethereum host: a program can ask for 8 MB of linear memory. Browser and server are the targets that can carry a WASM interpreter.

## Open questions

- Code prefixes per ArbOS version, the WASM features Nitro accepts, and the host-function list.
- Where activation records and the ink parameters sit in ArbOS state.
- Whether `eth_createAccessList` reports storage read by Stylus host functions and by ArbOS precompiles.
