: Ethereum

:: Threat Model

Considering the nature of a verifier, analysing and understaning the limit is critical. While the security of Colibri Stateless is similiar to the security of a light client, since it is based on the same foundation, there are specific risks to be aware of:

## 1. Security of the Sync Committee

### Description

The beacon light client relies on the aggregated BLS signature from a sync committee comprising 512 validators. Given that Ethereum currently has over 1 million active validators, this subset represents a small fraction of the total validator set. The concern arises from the possibility that a compromised or malicious sync committee could sign off on invalid blocks, potentially misleading the light client.

### Crypto-Economics

To compromise a light client, an attacker would need to produce a valid-looking signature from at least 2/3 of the sync committee (342 out of 512 validators). Given that committee members are sampled randomly from the global validator pool (\~1 million), the attacker would need to control a disproportionately large number of validators to have a meaningful chance of dominating the committee.

| Attacker Control | Expected Committee Members | 2/3 Majority Reached? |
| ---------------- | -------------------------- | --------------------- |
| 10%              | 51                         | ❌                     |
| 33%              | 169                        | ❌                     |
| 66%              | 338                        | ❌ (just under)        |
| 70%              | 358                        | ✅                     |

To reliably reach a 2/3 majority, an attacker would need to control **at least \~70%** of all validators.

* **Required validators**: \~700,000
* **Required ETH**: 700,000 × 32 = **22,400,000 ETH**
* **Estimated cost (at \$3,000/ETH)**: **\$67.2 billion**

This attack would also expose the attacker to:

* **Slashing penalties** for signing conflicting or invalid updates
* **Severe market impact**, since acquiring this much ETH would likely skyrocket the price
* **Community countermeasures**, including social recovery or hard forks

🔐 **Conclusion**: A sync committee compromise is considered *economically infeasible* under normal conditions, given the enormous capital requirements and the existential risks for the attacker.

### Mitigation

* **Random Selection**: Sync committee members are selected pseudo-randomly every 256 epochs (\~27 hours), making it statistically improbable for an attacker to consistently control the committee.

* **Economic Incentives**: Validators have significant ETH staked and face slashing penalties for malicious behavior, deterring attempts to compromise the sync committee.

## 2. Finality During LightClientUpdates

### Description

LightClientUpdates include both an `attestedHeader` and a `finalityHeader`. However, the proof for the `nextSyncCommittee` is based on the `attestedHeader`, which may not be finalized at the time of the update. This raises concerns about the reliability of the `nextSyncCommittee` if the `attestedHeader` is later reorged out of the chain.

### Mitigation

* **Temporal Stability**: The `nextSyncCommittee` is determined at the start of each sync committee period and remains constant for 256 epochs (\~27 hours), reducing the impact of short-term reorgs. [Wiki.js](https://www.inevitableeth.com/home/ethereum/network/consensus/sync-committee)

* **Economic Deterrents**: Validators are economically disincentivized from signing conflicting states due to the risk of slashing. [EIP-3321](https://github.com/ethereum/consensus-specs/issues/3321)

* **Finality Proofs**: Even in case of an reorg, the resulting pubkeys would not change since they are fixed for the period and so have the same security as a finality header, since the also rely on the BLS Signature of the 512 Validators.

## 3. Long Chain Attacks

### Description

Long-range attacks involve adversaries creating an alternative chain that diverges from the main chain, potentially deceiving light clients that have been offline for extended periods. This is a known vulnerability in proof-of-stake systems, especially concerning nodes that lack recent state information.

### Crypto-Economic Risk Analysis

Long-range or “long chain” attacks exploit the fact, that in Proof-of-Stake systems, old validators can sign alternative chains even if they no longer have any stake locked. Unlike short-term attacks on sync committees, long-range attacks don’t require control over live validators – they rely on historical keys and inactive signers.

This makes the **cost** of launching such an attack theoretically **much lower**, but introduces a different class of security assumption: **weak subjectivity**.

### Weak Subjectivity Period

A light client must regularly sync to a trusted checkpoint within the so-called **weak subjectivity period** (WSP). The WSP is the maximum safe offline time before finality guarantees weaken. If a client has been offline longer than the WSP, it can no longer be certain which chain is canonical, unless it verifies the root against a trusted source.

> ⏳ The length of the WSP depends on validator churn and activity, but is typically between **2 to 4 months** on Ethereum mainnet.

See the detailed analysis by Runtime Verification:
📄 [Weak Subjectivity Analysis](https://github.com/runtimeverification/beacon-chain-verification/blob/master/weak-subjectivity/weak-subjectivity-analysis.pdf)

### Attack Strategy

An attacker could attempt to:

1. Restore old validator keys (from backups or leaks)
2. Construct a valid-looking but non-canonical fork starting from a historic block
3. Use these old keys to sign a fork with "finalized" headers
4. Trick clients that haven’t synced recently and have no fresh trusted checkpoint

**Attack Cost**

* **No stake required**: Validators don’t need to have any ETH at stake; only old keys.
* **Infrastructure cost**: The attacker must simulate a chain many epochs long, including all block proposals and attestations.
* **Risk of detection**: Honest full nodes and checkpoint providers will reject the fork, so the attack only works against **isolated or offline clients**.
* **Long duration**: Building a convincing fork over months of slots is computationally expensive and non-trivial.

### Mitigation

* **Weak Subjectivity Checkpoints**: Users can configure trusted block hashes as starting points, ensuring that the light client syncs from a known good state. [Weak Subjectivity](https://ethereum.org/en/developers/docs/consensus-mechanisms/pos/weak-subjectivity)

* **Checkpoint Providers**: When the distance between the last trusted block and the current head exceeds the weak subjectivity period, the client consults multiple trusted checkpoint providers to verify the chain's validity.

