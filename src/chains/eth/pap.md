: Ethereum

:: Pragmatic Adaptive Privacy


This document describes the concrete implementation plan for Pragmatic Adaptive Privacy (PAP) within colibri-stateless, mapping each supported Ethereum RPC method to its privacy exposure and potential mitigation strategies.

See [Pragmatic Adaptive Privacy (PAP)](https://medium.com/corpus-core-insights/pragmatic-adaptive-privacy-pap-5c6f6080cb1a) for the high-level concept.

## Privacy Model

Each RPC method is analyzed along the two PAP axes:

- **Transport (T)**: Can an RPC provider link the request to a persistent identity? (who is asking)
- **Content (C)**: Can an RPC provider infer intent from the request content? (what is being prepared)

Privacy risk levels per method:
- **Identity Exposure**: The request parameters strongly correlate with the caller's own address or activity.
- **Intent Exposure**: The request reveals what the caller is about to do (e.g., trade, transfer, interact with a specific protocol).
- **Interest Exposure**: The request reveals which contracts, accounts, or state the caller is monitoring.

**Critical vs uncritical for PAP**: Some categories are **uncritical** from a pragmatic privacy perspective: the request content does not meaningfully reveal identity or intent, or the signal is so weak and universal that PAP mitigations are not worth the cost. These categories can be documented briefly; PAP design effort should focus on the critical ones (Account, Call, Logs, Transactions, Filters/Subscriptions when they carry log filters).

---

## RPC Methods by Technical Implementation

Methods are grouped by how they are implemented and verified in colibri-stateless (shared proof types, data sources, and request shapes). Privacy mitigations can often be designed per category.

---

### Account

**Implementation**: All methods query account state at a given block. Colibri uses **EthAccountProof** (Merkle-Patricia proof for account + optional storage slots). Same underlying data: account nonce, balance, storageRoot, codeHash; optionally specific storage slots or full code.

| Method | Proof type | Parameters |
|--------|------------|------------|
| `eth_getBalance` | EthAccountProof | `address`, `block` |
| `eth_getTransactionCount` | EthAccountProof | `address`, `block` |
| `eth_getCode` | EthAccountProof | `address`, `block` |
| `eth_getStorageAt` | EthAccountProof | `address`, `position`, `block` |
| `eth_getProof` | EthAccountProof | `address`, `storageKeys[]`, `block` |

**Privacy Exposure** (varies by method):

- **eth_getBalance**
  - **Identity** (high): Queried address is usually the caller's own or a counterparty. Repeated queries build a strong profile.
  - **Intent** (medium): Balance checks often precede transfers; timing and frequency reveal activity patterns.

- **eth_getTransactionCount**
  - **Identity** (high): Nonce is almost always queried for the caller's address before sending a tx. One of the strongest identity signals.
  - **Intent** (high): Strong signal that a transaction is imminent.

- **eth_getCode**
  - **Identity** (low): Contract addresses are public.
  - **Interest** (medium): Reveals which contracts the user is analyzing; in context, indicates protocol interest.

- **eth_getStorageAt**
  - **Identity** (low–medium): Storage slot may encode user-specific key (e.g. mapping slot from caller address).
  - **Interest** (high): Exact slot reveals precise state interest (balances, positions, allowances, prices).

- **eth_getProof**
  - **Identity** (medium): Queried address may be caller or monitored counterparty.
  - **Interest** (high): Storage keys reveal which contract state the user cares about (DeFi positions, governance, etc.).

#### PAP approach: Account (uniform getProof + code cache + address noise)

Account requests are the most common identity-revealing calls: the queried address is almost always the user's own or a closely related one. The approach focuses on two aspects: (1) hiding **what the user intends** (intent) and (2) making it harder to pinpoint **which account the user cares about** (identity).

**1. Uniform method: always use eth_getProof**

Regardless of whether the application calls `eth_getBalance`, `eth_getTransactionCount`, `eth_getCode`, `eth_getStorageAt`, or `eth_getProof`, the Verifier internally **always** issues `eth_getProof`. This is natural because verification already requires the proof; the only change is to stop using the specialized methods entirely.

Effect: the RPC provider only ever sees `eth_getProof(address, storageKeys[], block)` and cannot infer the caller's intent from the method name alone:

| Original method | Intent signal (without PAP) | With uniform getProof |
|----------------|----------------------------|----------------------|
| `eth_getTransactionCount` | User is about to send a tx | Hidden — just another getProof |
| `eth_getBalance` | User is checking own balance (received funds?) | Hidden |
| `eth_getCode` | User is checking if address is EOA or contract | Hidden |
| `eth_getStorageAt` | User is reading specific contract state | Hidden (merged into storageKeys) |

**2. Code cache**

Contract code rarely changes (upgradeable proxies being the exception). Cache the code locally and only verify that the **codeHash** (returned in every account proof) still matches. If it matches, serve the code from cache; if not, re-fetch. This eliminates repeated code fetches and means the provider never sees a dedicated "give me the code" request — just normal getProof calls whose codeHash field is checked locally.

**3. Address noise — hiding which account the user cares about**

Even with uniform getProof, the provider still sees the queried **address**. This is the harder problem. Mitigation: add **extra addresses** to the request batch (or issue parallel getProof calls for dummy addresses), so the provider sees multiple accounts and cannot easily tell which one is the real target.

Choosing noise addresses:

| Strategy | Rationale |
|----------|-----------|
| **Well-known token contracts** (USDC, WETH, DAI, …) from a curated account list | Always exist; universally relevant; plausible for any user. Provider cannot distinguish "checking USDC balance" from noise. |
| **Random addresses** | Useful to mimic the pattern of querying a newly created wallet (empty/non-existent accounts). A provider seeing getProof for an unknown address cannot tell if it is a new wallet or noise. |
| **Addresses from recent blocks** (e.g. tx senders/receivers) | Known to exist and be active; plausible queries. Can be harvested from cached block data (see Transactions section). |
| **Addresses seen in previous calls/logs** (e.g. contracts the user interacted with before) | Already known to the Verifier; reusing them as noise adds plausibility without extra discovery cost. |

**4. Strategy per account type**

- **Own EOA** (highest risk — nonce query = imminent tx): Always batch with noise addresses. Timing still leaks (nonce query right before sendRawTransaction), but the provider cannot be sure *which* of the queried addresses is the sender.
- **Counterparty / contract**: Batch with well-known token contracts or addresses from the same block. The provider sees "user queried 5 accounts in block N" rather than "user queried exactly this DeFi contract."
- **Rarely used accounts** (e.g. personal multisig): These stand out because they are uncommon. Add more noise (e.g. other similarly uncommon contracts or random addresses) to dilute the signal. Accept that a determined observer could still flag unusual addresses over time.

**Limits**:

| Limit | Description | Mitigation / acceptance |
|-------|-------------|--------------------------|
| **First request / cold cache** | No cached code, no prior proof data. The initial getProof reveals the exact address. | Accept for the first request per session; benefit grows with continued use. |
| **Noise quality** | If noise addresses are poorly chosen (all empty, or always the same set), the provider can learn to filter them out. | Rotate noise set; mix well-known contracts + random + recently seen addresses. Make set non-deterministic per session. |
| **Rare accounts** | A seldom-queried account (e.g. personal multisig) can be flagged even among noise; the noise addresses may be obviously unrelated. | Accept as a pragmatic limit. For high-value targets, combine with T1/T2 transport measures (multi-provider, Tor). |
| **Bandwidth** | Each extra getProof adds data. Account proofs are relatively small (~1–2 KB), so adding 3–5 noise addresses per real request is manageable. | Keep noise count bounded (e.g. 3–8 extra addresses). Tune based on acceptable overhead. |

**Effort to de-anonymize**: Provider sees a batch of getProof calls for N addresses. Identifying the real target requires correlating across sessions, timing, and cross-referencing with other request types. Significantly harder than seeing `eth_getTransactionCount(myAddress)`. For rare accounts, correlation over time is still feasible; for common patterns (wallet checking own balance, interacting with popular contracts), the noise is effective.

---

### Call

**Implementation**: All methods execute (or simulate) a call in the EVM at a given block. Colibri uses **EthCallProof**. Same execution model; only the use of the result differs (return value vs. gas estimate vs. access list).

| Method | Proof type | Parameters |
|--------|------------|------------|
| `eth_call` | EthCallProof | `transaction object`, `block` |
| `colibri_simulateTransaction` | EthCallProof | `transaction object` |
| `eth_estimateGas` *(not yet verifiable)* | EthCallProof | `transaction object` |
| `eth_createAccessList` *(not yet verifiable)* | EthCallProof | `transaction object` |

**Privacy Exposure**:

- **eth_call**
  - **Identity** (medium): `from` (if set) reveals caller; `to` + calldata can be correlated across sessions.
  - **Intent** (high): Calldata encodes exact function and params — prices, swaps, allowances, governance. Very intent-revealing.

- **colibri_simulateTransaction**
  - **Identity** (high): Full transaction including sender.
  - **Intent** (very high): Pre-flight for an exact transaction; provider sees the full action. Maximum intent leakage.

- **eth_estimateGas** / **eth_createAccessList**
  - **Identity** (high): Full transaction details including sender.
  - **Intent** (high): Exact action being prepared; gas/access-list queries strongly signal imminent submission.

#### PAP approach: Call (storage cache + optimistic execution + noisy verification)

Call (and simulation/estimateGas/createAccessList) is the hardest and most critical category: the calldata encodes the user’s intent very directly. Hiding *what* is being called while still verifying execution is the goal.

**Current flow**: Prover runs `eth_createAccessList`, then for each account and storage slot does `eth_getProof`, and sends everything together with the block proof to the Verifier. The Verifier verifies storage values and executes the call in the local EVM.

**Proposed mechanism**:

1. **Storage cache (Verifier)**: Maintain a cache of **verified** storage values (contract address + slot → value, and optionally storageRoot per contract). No raw unverified data.
2. **First request for a contract**: No cache yet. Execute as today: Prover does createAccessList + getProof for each slot, Verifier verifies and executes. After a successful run, **store the verified storage values** (and optionally the contract’s storageRoot) in the cache.
3. **Subsequent requests (optimistic execution)**:
   - Use **cached** storage values to run the call **locally in the EVM** without asking the Prover for the full access list upfront. No `eth_createAccessList` or bulk getProof for this call.
   - During execution, **collect all storage slots** that are actually read.
   - If we hit a slot **not in cache** → issue a single `eth_getProof` to fetch it (this can still leak that we need that slot, but over time and across many slots the signal is diluted).
4. **Verification after execution**: Even when all slots came from cache, we must **verify** that state did not change. So we still call `eth_getProof` for the storage slots that were used — but **add extra, dummy storage slots** to the same request. The provider then sees a set of slots and cannot be sure which ones were actually needed for the call. **Constraint**: the extra slots must **exist** in the trie (have real values). Otherwise a provider could ignore non-existent slots and infer the real ones. Idea: use the **Merkle proof structure** — branch nodes expose up to 16 child hashes, so we can discover which branches exist and choose additional slots that actually exist in the trie for that contract.
5. **Strategy per contract type**:
   - **Stable contracts** (e.g. MultisigWallet): Cache the contract’s **storageRoot**. Before reusing cached storage, check whether the storageRoot has changed (e.g. via a lightweight account proof). If unchanged, skip re-fetching and run optimistically; verification can be minimal or deferred. Works well when storage rarely changes.
   - **Volatile contracts** (e.g. USDC balance): Assume storage changes often. Always use the “verify with extra storage slots” strategy; avoid relying on storageRoot stability.
   - Strategy can be **whitelist-based**: known stable vs. known volatile contracts, or heuristics (e.g. how often cached values were invalidated).

**Effect**: The first call for a contract still reveals full intent (createAccessList + getProof). Later calls for the same or related contracts can be executed locally with cache; only (noisy) getProof requests are sent, so the provider sees “a set of storage slots” rather than “this exact call with this calldata”. Tx simulations and repeated eth_calls (e.g. in a wallet UI) benefit most.

**Limits**:

| Limit | Description | Mitigation / acceptance |
|-------|-------------|--------------------------|
| **First request** | The initial call for a contract still uses the current flow (createAccessList + getProof). Full intent is visible once per contract. | Accept; document that the main gain is for **repeated** calls and simulations (e.g. approval flows, simulation loops, UI polling). |
| **Finding real dummy slots** | Extra slots we add for noise must exist in the trie, or the provider could filter them out and identify the real slots. | Use Merkle proof structure (branch nodes, existing paths) to discover real slots; or reuse slots seen in previous proofs for this or other contracts. Hardest part of the design; may need contract-specific or trie-walk heuristics. |
| **Performance** | Cache hits make subsequent calls fast (local EVM + one noisy getProof). Cache misses during execution add getProof round-trips. | Accept; for hot contracts the cache warms up quickly. |

**Effort to de-anonymize**: After warm cache, the provider only sees getProof(account, [slot1, slot2, …, slotN]) with N > actually needed. Correlating which subset mattered requires cross-request analysis and is harder than seeing createAccessList + full call. First call remains weak.

**Optimizations**:
- Reuse slots from **other contracts’ proofs** (e.g. same block, or from previous calls) as dummy slots — they exist and diversify the set.
- **Batching / jitter**: If multiple calls happen close together, batch or slightly delay getProof requests so the pattern is less “one call ↔ one proof”.
- Same approach applies to **colibri_simulateTransaction** (it is an eth_call under the hood); estimateGas/createAccessList would follow the same pattern once verifiable.

---

### Logs

**Implementation**: Both methods return event logs matching a filter. Colibri uses **ListEthLogsBlock** (logs with block proof). Same filter shape: `address`, `topics[]`, `fromBlock`, `toBlock`.

| Method | Proof type | Parameters |
|--------|------------|------------|
| `eth_getLogs` | ListEthLogsBlock | `filter` (address, topics, fromBlock, toBlock) |
| `eth_verifyLogs` | ListEthLogsBlock | same filter shape |

**Privacy Exposure**:

- **eth_getLogs** / **eth_verifyLogs**
  - **Identity** (medium–high): Topic filters often include the user's address (e.g. `Transfer` to/from). Address filter reveals monitored contracts.
  - **Interest** (high): Contract + topics reveal which protocol activity is tracked — swaps, liquidations, governance, token transfers.

#### PAP approach: Logs (Bloom filter + noise)

**Goal**: Make it harder for the RPC provider to infer which specific events the user cares about. The provider should see a broader query that matches more logs than the user actually needs; the client then filters the result locally.

**Mechanism**:

- **Filter → Bloom / filter set**: From the user’s exact filter (address, topics[], fromBlock, toBlock) we derive the corresponding Bloom filter (or a small set of possible filters) that would match the desired events.
- **Add noise by removing bits**: Adding noise to a Bloom filter means **removing set bits** from it — each removed bit widens the match, causing more events to pass the filter. This is the key insight: the Bloom filter for the exact query has certain bits set; we **clear a percentage of those bits**, making the filter more permissive. The fewer bits remain set, the more events match; the more bits we clear, the broader the result.
- **Spectrum from exact to full block**: At one extreme (0% removed) the Bloom matches only the exact events — no privacy gain. At the other extreme (100% removed, all-zero Bloom) the filter matches **every** event in every block, which is equivalent to requesting `eth_getBlockReceipts` for each block in the range — maximum privacy but also maximum overhead. Any value in between is a trade-off.
- **Configurable noise level**: The percentage of bits to remove should be **configurable** (e.g. `bloomNoisePercent: 30` means clear 30% of the set bits). This gives applications and users a direct knob to tune the privacy/performance trade-off. A sensible default (e.g. 20–40%) provides meaningful noise without excessive overhead; users with higher privacy requirements can increase it.
- **Provider returns superset**: The RPC returns all logs matching the weakened Bloom (more than the user needs).
- **Local filtering**: The Verifier/client still knows the exact original filter. It runs through the returned list and keeps only the logs that match the real filter. Only those are returned to the application.

**Effect**: The provider sees a query with a weaker Bloom that matches many events across more contracts/topics and cannot tell which of the returned logs the user was actually interested in. Identity and intent leakage are reduced (content privacy C1-style: abstracted/noisy read). The configurable noise level lets the system adapt to different risk profiles — from "slightly noisy" for routine queries to "practically all receipts" for high-sensitivity operations.

**Limits**:

| Limit | Description | Mitigation / acceptance |
|-------|-------------|--------------------------|
| **Noise too low** | If the noisy filter still matches only one (or very few) events, the provider can infer the user’s interest. | Always add enough noise so that the result set is not uniquely identifying. Prefer **too much** over too little: better to return extra logs than to leak the single event of interest. |
| **Noise too high** | Too much noise → very broad filter → many matching logs → more bandwidth and client-side work. | Tune noise so the result set stays manageable (e.g. cap block range or broaden only within bounds). Accept a performance trade-off; document that UX may vary with filter shape and chain activity. |
| **Tuning** | Noise strategy (how many extra addresses/topics, how to choose them) affects both privacy and performance. | Make noise configurable or adaptive (e.g. ensure minimum result set size per block range). Prefer simple, deterministic rules that are easy to reason about. |

**Effort to de-anonymize**: With a well-tuned noisy filter, an observer sees a broad log query. Linking the request to a specific event or user intent requires correlating the superset with other signals; significantly harder than seeing the exact filter. If noise is too low and only one event matches, leakage reverts to the non-PAP case.

**eth_verifyLogs — different use case**: Here the flow is usually: events come from an **indexer**, and the client sends them to the **Prover** to obtain a proof for verification. So the client has already shared the event set with the indexer; the additional privacy concern is the **Prover**. To reduce leakage toward the Prover, the client should **add extra events it is not interested in** to the payload sent for verification. The Prover then sees a superset of events and cannot tell which subset the client actually cares about. Same principle as with getLogs (noise so the observer sees more than the real interest), but applied to the verification request: pad with additional events so the result set is not uniquely identifying.

**Implementation detail — API extension**: Standard `eth_getLogs` accepts only concrete values (`address`, `topics[]`, `fromBlock`, `toBlock`), not a Bloom filter. For the PAP approach we extend the request with an optional property **`bloomFilters`: []**, which carries the list of possible (e.g. widened) filters used for the noisy query. The Prover/Verifier can interpret this and return all logs matching any of these filters; the client then filters locally to the exact set. Open question: keep this extension **internal** to the colibri Prover/Verifier only, or **propose an EIP** so that other RPC providers could implement the same pattern and improve log privacy across the ecosystem.

---

### Transactions

**Implementation**: Lookup of a single transaction — by hash or by block + index. Colibri uses **EthTransactionProof** (transaction inclusion in block) and **EthReceiptProof** (receipt inclusion). Transaction-by-block methods use the same proof type as by-hash; the client just supplies block + index instead of hash.

| Method | Proof type | Parameters |
|--------|------------|------------|
| `eth_getTransactionByHash` | EthTransactionProof | `transactionHash` |
| `eth_getTransactionByBlockHashAndIndex` | EthTransactionProof | `blockHash`, `index` |
| `eth_getTransactionByBlockNumberAndIndex` | EthTransactionProof | `blockNumber`, `index` |
| `eth_getTransactionReceipt` | EthReceiptProof | `transactionHash` |

**Privacy Exposure**:

- **eth_getTransactionByHash** / **eth_getTransactionReceipt**
  - **Identity** (high): Requested tx is very likely one the user sent, received, or is involved with. Hash directly identifies an on-chain action.
  - **Intent** (medium): Polling for receipt reveals recent submission and waiting for confirmation.

- **eth_getTransactionByBlockHashAndIndex** / **eth_getTransactionByBlockNumberAndIndex**
  - **Identity** (medium): Block+index still reveals interest in a specific tx; less direct than hash.
  - **Interest** (medium): Specific block and position indicate targeted monitoring.

#### PAP approach: Transaction lookup privacy

**Goal**: Avoid revealing which specific transaction (or receipt) the user cares about. The RPC provider should only see requests for full blocks or block-level data, not individual tx hashes.

**Mechanism**:

- **Tx cache (Verifier)**: Maintain a mapping `txHash → blockNumber` only—no need to cache full transactions. On every new block (e.g. every ~12s), refresh the cache with the new block’s tx hashes (and optionally evict entries older than a window, e.g. last 100–200 blocks).
- **Lookup flow**:
  1. User requests transaction (or receipt) by hash.
  2. Verifier looks up `blockNumber` in the local tx cache.
  3. If found: request **full block** via `eth_getBlockByNumber(blockNumber, true)` from Prover/RPC, then extract the transaction locally. For receipts: request **all receipts** for that block (e.g. `eth_getBlockReceipts(blockNumber)`), then extract the receipt locally.
  4. The provider only sees “block N requested” (and possibly “receipts for block N”), not which tx or receipt the user needed.
- **Cold start / cache warming**: When the cache is empty (e.g. app start), the Prover can expose an endpoint that returns the mapping `txHash → blockNumber` for the last N blocks (e.g. 50–100) in one response. The Verifier requests this bulk mapping (e.g. periodically or on first tx lookup), updates the cache, then uses the same block-based lookup as above. The provider does not see a single tx hash—only “mapping for blocks [latest−N, latest]”.

**Effect**: For requests inside the cache window, the provider cannot tell which of the many transactions in the block the user is interested in. Identity and intent leakage drop from “user asked for tx 0x…” to “user asked for block N” (which is PAP-uncritical).

**Limits**:

| Limit | Description | Mitigation / acceptance |
|-------|-------------|--------------------------|
| **Outside cache window** | Transactions older than the cached range (e.g. >100 blocks) require a fallback. A direct `eth_getTransactionByHash` or “which block is this tx in?” would reveal the hash. | Accept leakage for the long tail. In practice a large share of lookups (e.g. ~98%) are for recent blocks (pending confirmation, history in wallet). Optimize for the common case; document that old tx lookups may leak. |
| **Data volume** | Fetching a full block (and all receipts) is larger than a single tx/receipt. Typical block: tens to a few hundred transactions; size is manageable. | Accept as trade-off. Optionally cache recently fetched blocks in the Verifier so multiple lookups in the same block do not trigger repeated block fetches. |
| **Receipts verification** | `eth_getBlockReceipts` is not yet implemented in colibri. Verification is straightforward: the Prover sends all serialized receipts for the block; the Verifier builds the Merkle trie from them and compares the root to `receipts_root` in the execution payload (same approach as for single receipt proofs). | Implement `eth_getBlockReceipts` support and verification; see [GitHub issue #179](https://github.com/corpus-core/colibri-stateless/issues/179). |

**Effort to de-anonymize**: For requests inside the cache window, an observer only sees block (and block-receipt) requests. Linking a specific tx to the user would require correlating timing with block content and other side channels—much harder than seeing the explicit `eth_getTransactionByHash(txHash)` call. For requests outside the window, current design accepts that the hash may be visible (e.g. one direct request); effort to exploit remains low for that minority case.

**Optimizations**:

- **Same-block reuse**: After fetching a block for one tx, keep the block (or at least the tx list) in a short-lived cache. Further lookups for other txs in the same block can be served locally without another `eth_getBlockByNumber` (and same for receipts if we cache “receipts for block N”).
- **Periodic bulk mapping**: Verifier can periodically (e.g. every new block) request “mapping for last N blocks” from the Prover and refresh the tx cache. Then most user lookups hit the cache and only trigger block (and block-receipt) requests, never a per-tx query.
- **Wallet UX**: No change from the user’s perspective: they still request “my” transaction or receipt by hash. Latency may increase slightly when a block must be fetched; caching and reuse keep this minimal for typical usage.

---

### Block — **PAP: Uncritical**

**Why uncritical**: Block data is public and shared. Requesting a block by hash or number does not reveal who the user is or what they intend to do. Every client needs `eth_blockNumber` and `eth_getBlockByNumber("latest", ...)`; the parameters are generic. Querying a specific historical block is at most a weak signal (e.g. block containing a tx), not user-specific. For PAP we can treat this category as low priority: no special mitigation needed beyond normal transport choices.

**Implementation**: Read block metadata or full block data. Colibri uses **EthBlockProof** (block header + body) or **EthBlockNumberProof** (latest block number only). Block-level methods share the same execution payload / block structure.

| Method | Proof type | Parameters |
|--------|------------|------------|
| `eth_blockNumber` | EthBlockNumberProof | — |
| `eth_getBlockByHash` | EthBlockProof | `blockHash`, `includeTransactions` |
| `eth_getBlockByNumber` | EthBlockProof | `blockNumber`, `includeTransactions` |
| `eth_getBlockTransactionCountByHash` / `eth_getBlockTransactionCountByNumber` *(not yet verifiable)* | — | `blockHash` / `blockNumber` |
| `eth_getBlockReceipts` *(not yet verifiable)* | — | `blockNumber` |

---

### Filters

**Implementation**: Stateful log/block/pending filters. Creation carries the same filter parameters as `eth_getLogs`; polling only sends a filter ID. No proof in colibri for filter results (currently Void); privacy is determined by the **creation** call.

| Method | Proof type | Parameters |
|--------|------------|------------|
| `eth_newFilter` | Void | `filter` (address, topics, fromBlock, toBlock) |
| `eth_newBlockFilter` | Void | — |
| `eth_newPendingTransactionFilter` | Void | — |
| `eth_getFilterChanges` | Void | `filterId` |
| `eth_getFilterLogs` | Void | `filterId` |
| `eth_uninstallFilter` | Void | `filterId` |

**Privacy Exposure**:

- **eth_newFilter**
  - **Identity** (medium): Same as `eth_getLogs` if address/topics are set.
  - **Interest** (medium): Filter params reveal what the user monitors over time.

- **eth_newBlockFilter** / **eth_newPendingTransactionFilter**
  - **Identity** (none): No address/topic params.
  - **Interest** (low): Generic.

- **eth_getFilterChanges** / **eth_getFilterLogs** / **eth_uninstallFilter**
  - **Identity** (low): Only filterId; provider can correlate to creation.
  - **Interest** (low–medium): Inherited from filter creation; polling pattern (timing, frequency) adds little.

#### PAP approach: Filters and Subscriptions

Filters and subscriptions are wrappers around the same log/event data that `eth_getLogs` handles. The PAP strategy depends on **where** the filter lives:

**Option A — Local filter + polling via eth_getLogs (preferred)**

The filter exists **only in the client**. Instead of registering it with the provider, the Verifier periodically calls `eth_getLogs` (with the noisy Bloom filter approach described in the Logs section) and filters the results locally.

- **Privacy advantage**: Each polling request is a standalone `eth_getLogs` call. Requests can be **distributed across different providers** (T1 transport), so no single provider sees the full polling history. The provider never learns that the user has a persistent interest; each call looks like an independent log query.
- **Important: stable noise per filter**. When polling repeatedly for the same filter, the noise (extra addresses/topics in the Bloom) must stay **constant** across requests. If the noise changes each time, a provider who sees multiple requests could intersect them and identify the bits that remain constant — those are the real filter. So: generate the noisy Bloom **once** when the filter is created, then reuse it for all subsequent polling calls. Only regenerate the noise when the filter itself changes.
- Block and pending-transaction filters (`eth_newBlockFilter`, `eth_newPendingTransactionFilter`) are trivially local: just track the latest known block number and request new blocks/pending txs since the last poll. No privacy concern (see Block — uncritical).

**Option B — Remote filter (eth_newFilter on provider)**

If the filter must be registered remotely (e.g. because the provider does not support repeated `eth_getLogs` efficiently, or because the app uses `eth_getFilterChanges`), then the same Bloom filter noise approach from the Logs section applies at creation time:

- `eth_newFilter` is called with the **widened** filter (noisy Bloom / extra addresses+topics).
- `eth_getFilterChanges` / `eth_getFilterLogs` only send the filter ID — no additional content leaks beyond the creation call.
- **Drawback vs. Option A**: All polling goes to the **same** provider (the one holding the filter), so T1 transport distribution is not possible. The provider also sees the polling frequency and timing, which reveals how actively the user is monitoring.

**Subscriptions** (`eth_subscribe` / `eth_unsubscribe`) are functionally equivalent to remote filters over a persistent connection (websocket). Same rules: widen the subscription filter with noise at creation time. Subscriptions have the same drawback as Option B — they are tied to a single provider and the connection itself is a persistent identity link.

**Recommendation**: Prefer **Option A (local filter + polling)** whenever possible. It gives the best privacy (requests distributable, no persistent filter on any provider, stable noise). Fall back to Option B / subscriptions only when the application requires it (e.g. low-latency push notifications).

**Limits**:

| Limit | Description | Mitigation / acceptance |
|-------|-------------|--------------------------|
| **Stable noise = fingerprint** | Reusing the same noisy Bloom across polls means the Bloom itself becomes a session fingerprint for the provider who sees multiple polls. | Acceptable if combined with T1 (distribute polls across providers) — no single provider sees enough repetitions. If polling a single provider, the fingerprint risk is the trade-off for not leaking the real filter via intersection. |
| **Polling overhead** | Local polling means repeated `eth_getLogs` calls rather than efficient server-side diff. | Tune polling interval; use block range (fromBlock, toBlock) to limit per-request cost. For most wallet use cases, polling every ~12s (one block) is sufficient. |
| **Subscriptions are inherently tied to one provider** | Cannot distribute; the persistent connection itself is identifying. | Document as a known limit. For high-privacy needs, avoid subscriptions and use local polling instead. |

---

### Gas and Fees — **PAP: Uncritical**

**Why uncritical**: Fee data is generic chain state. All clients need it for UI or before sending a transaction; there are no user-specific parameters. The only marginal signal is that someone calling `eth_gasPrice` or `eth_feeHistory` might be about to send a transaction—but this is a very weak, universal signal and not actionable for profiling. PAP does not need to prioritize these methods.

**Implementation**: Read chain fee state; no account or execution proof. Not yet verifiable in colibri; when added, likely block-header or dedicated fee proof.

| Method | Proof type | Parameters |
|--------|------------|------------|
| `eth_gasPrice` *(not yet verifiable)* | — | — |
| `eth_maxPriorityFeePerGas` *(not yet verifiable)* | — | — |
| `eth_feeHistory` *(not yet verifiable)* | — | `blockCount`, `newestBlock`, `rewardPercentiles` |
| `eth_blobBaseFee` *(not yet verifiable)* | EthBlockHeaderProof | — |

---

### Uncles (Ommers) — **PAP: Uncritical**

**Why uncritical**: Uncle/ommer data is public block metadata. It does not link to a user identity or to intent; at most it might indicate mining/validator tooling, which is negligible for read-privacy. No PAP mitigation needed.

**Implementation**: Uncle block headers by block + index. Not yet verifiable in colibri. Generic block metadata.

| Method | Proof type | Parameters |
|--------|------------|------------|
| `eth_getUncleByBlockHashAndIndex` / `eth_getUncleByBlockNumberAndIndex` | — | `blockHash`/`blockNumber`, `index` |
| `eth_getUncleCountByBlockHash` / `eth_getUncleCountByBlockNumber` | — | `blockHash`/`blockNumber` |

---

### Write (Submit)

**Implementation**: Submit a signed transaction to the network. Not a read; no proof. Included for completeness and because it is the ultimate identity+intent leak.

| Method | Proof type | Parameters |
|--------|------------|------------|
| `eth_sendRawTransaction` | — | `signedTransactionData` |

**Privacy Exposure**:

- **Identity** (very high): Signed tx contains sender; write operation.
- **Intent** (very high): Full transaction visible. Outside PAP read-privacy scope.

---

## Local Methods (no RPC round-trip)

Resolved entirely inside colibri-stateless; no data sent to an RPC provider.

| Method | Description |
|--------|-------------|
| `eth_chainId` | Configured chain ID |
| `eth_accounts` | Locally managed accounts |
| `eth_protocolVersion` | Protocol version |
| `web3_clientVersion` | Client version string |
| `web3_sha3` | Keccak256 locally |
| `net_version` | Network version |
| `colibri_decodeTransaction` | Decode transaction data locally |

**Privacy**: No exposure — these never leave the client.

---

## Transport Privacy (T-axis): Hiding Who Is Asking

All content-level mitigations (C-axis) described above reduce **what** a provider can infer from a request. But even with perfect content privacy, the provider still sees **who** is asking — via IP address, TLS fingerprint, connection timing, and session correlation.

The T-axis addresses this by decoupling the request from the requester's identity.

### Architecture: Transport is a binding concern

Colibri's C core does **no network I/O**. It only describes what data it needs (method, URL, payload, encoding) and the **host system** (the language binding — TypeScript/JS, Python, Kotlin, Swift, C) executes the actual HTTP requests and returns the responses. The C core is completely agnostic about how requests are transported.

This means: **transport privacy is implemented entirely in the bindings**. The C core does not need to change. Each binding already maintains a list of RPC providers / Provers and selects which source to use for each request. Adding Tor (or other anonymization) means replacing or wrapping the HTTP transport layer in the binding.

### T1: Multi-provider distribution

The simplest transport-level improvement — and already partially supported by the existing architecture:

- Each binding holds a **list of RPC providers** (and optionally Provers).
- Instead of sending all requests to a single provider, the binding **distributes requests across providers**. Different requests in the same proof-generation cycle can go to different providers.
- **Effect**: No single provider sees the full set of requests for a given action. Correlation across providers is much harder than correlation within a single provider's logs.
- **Implementation effort**: Low — most bindings already support multiple providers for retry/fallback. The change is to actively **rotate** or **randomize** provider selection rather than using a fixed primary with fallback.

### T2: Tor as transport layer

For stronger transport privacy, route requests through the **Tor network** so the provider cannot see the client's IP address.

**How it works per binding**:

| Binding | Tor integration approach |
|---------|------------------------|
| **TypeScript/JS (Node.js)** | Use a SOCKS5 proxy client (e.g. `socks-proxy-agent`) pointed at a local Tor daemon (`127.0.0.1:9050`). Replace the default `fetch` / `http.request` with the proxied version. |
| **TypeScript/JS (Browser/React Native)** | Direct Tor integration in browsers is not practical. Options: (1) Route requests through a backend proxy that itself uses Tor, or (2) Use Tor-enabled browser (Tor Browser). For React Native: bundle a Tor client library (e.g. `AaltoTorLib` on Android, `Tor.framework` on iOS). |
| **Python** | Use `requests` with a SOCKS5 proxy (`proxies={'https': 'socks5h://127.0.0.1:9050'}`) or use `stem` to manage a Tor process programmatically. |
| **Kotlin/Android** | Use `OkHttp` with a SOCKS proxy pointed at a local Tor daemon. On Android, bundle Tor via `tor-android` (Guardian Project) or use Orbot's proxy. |
| **Swift/iOS** | Use `URLSession` with a SOCKS5 proxy configuration, or integrate `Tor.framework` (iCepa project) for embedded Tor. Apple's `NEVPNProtocol` can also be used for VPN-based routing. |
| **C/C++** | Use `libcurl` with SOCKS5 proxy (`curl_easy_setopt(curl, CURLOPT_PROXY, "socks5h://127.0.0.1:9050")`), or link against a Tor client library directly. |

**Common pattern**: In all cases, the binding's HTTP execution layer is the only thing that changes. The flow remains:

1. C core returns pending requests (type, URL, payload).
2. Binding selects a provider from its list.
3. **Binding routes the HTTP request through Tor** (SOCKS5 proxy) instead of connecting directly.
4. Response is passed back to the C core as usual.

### Combining T1 and T2

T1 (multi-provider) and T2 (Tor) are **composable**:

- **T1 alone**: Requests distributed across providers, but each provider sees the client's IP. Reduces per-provider correlation; does not prevent IP-based attribution.
- **T2 alone**: All requests go through Tor, hiding the IP. But if all go to the same provider, the provider can still correlate by session/timing.
- **T1 + T2**: Requests distributed across providers **and** routed through Tor. The provider sees neither the client IP nor the full request pattern. Best transport privacy achievable without specialized infrastructure.

### Circuit rotation and request correlation

Tor assigns a **circuit** (path through the network) per connection. If multiple requests share the same circuit, the exit node (and the provider) can correlate them by source.

- **Recommendation**: Use a **new Tor circuit for each provider** (or even per request batch). Most Tor client libraries support circuit isolation via SOCKS5 authentication (different username/password per circuit).
- This prevents an exit node from linking requests that go to different providers, and prevents a provider from correlating requests across different circuits.

### Limits

| Limit | Description | Mitigation / acceptance |
|-------|-------------|--------------------------|
| **Latency** | Tor adds 200–800ms per request (3-hop circuit). For proof generation involving multiple round-trips, this compounds. | Accept for high-privacy use cases. Make Tor **optional and configurable** — users choose privacy vs. speed. For routine wallet operations, T1 alone may be sufficient. |
| **Availability** | Tor is not available in all environments (corporate firewalls, some mobile networks, restrictive countries). | Fall back gracefully to T1 (multi-provider) or direct connection. Never fail hard if Tor is unavailable. |
| **Browser environment** | Browsers cannot use SOCKS5 proxies directly. Tor integration requires external infrastructure or a Tor-enabled browser. | For browser-based dApps: document as a known limitation. Recommend Tor Browser for privacy-sensitive users, or a backend proxy for app-controlled routing. |
| **Tor fingerprinting** | A provider may detect Tor exit nodes (IP lists are public) and treat them differently (rate-limit, block, or flag). | Accept as trade-off. Combine with T1 (not all requests via Tor). Some providers may block Tor; the binding should fall back to the next provider. |
| **Mobile resource usage** | Bundling a Tor daemon on mobile (Android/iOS) adds binary size (~5–10 MB) and background resource usage. | Make Tor an optional dependency. Only include it when the user explicitly enables transport privacy. |

### Configuration

Transport privacy should be configurable per binding, ideally with a simple API:

```
{
  "transport": {
    "mode": "tor" | "multi-provider" | "direct",
    "tor": {
      "socksProxy": "127.0.0.1:9050",
      "circuitIsolation": true
    },
    "providers": [
      "https://rpc1.example.com",
      "https://rpc2.example.com",
      "https://rpc3.example.com"
    ],
    "rotationStrategy": "random" | "round-robin"
  }
}
```

Default should be `"multi-provider"` (T1) — meaningful privacy improvement with no latency penalty. `"tor"` (T2) is opt-in for users who need stronger anonymity.

---

## Summary: Privacy Risk by Method

| Risk | Methods |
|------|--------|
| **Very High** | `colibri_simulateTransaction`, `eth_sendRawTransaction` |
| **High** | `eth_getBalance`, `eth_getTransactionCount`, `eth_call`, `eth_estimateGas`, `eth_createAccessList`, `eth_getTransactionReceipt`, `eth_getTransactionByHash` |
| **Medium** | `eth_getProof`, `eth_getStorageAt`, `eth_getLogs`, `eth_verifyLogs`, `eth_getTransactionByBlockHashAndIndex`, `eth_getTransactionByBlockNumberAndIndex`, `eth_newFilter` |
| **Low** | `eth_getCode`, `eth_getBlockByHash`, `eth_getBlockByNumber`, `eth_getBlockReceipts`, `eth_gasPrice`, `eth_maxPriorityFeePerGas`, `eth_feeHistory`, `eth_blobBaseFee` |
| **None** | `eth_blockNumber`, `eth_chainId`, `eth_accounts`, `web3_sha3`, `net_version`, `web3_clientVersion`, `eth_protocolVersion`, `colibri_decodeTransaction` |

**PAP-uncritical categories** (no special mitigation needed): **Block**, **Gas and Fees**, **Uncles**. See the "Why uncritical" notes in those sections.
