# C4 (corpus core colibri client)

![ETH2.0_Spec_Version 1.4.0](https://img.shields.io/badge/ETH2.0_Spec_Version-1.4.0-2e86c1.svg)

## Index

- [Index](#index)
- [Concept](#concept)
    - [Updating the sync committee](#updating-the-sync-committee)
    - [Proof Requests](#proof-requests)
- [Building](#building)
    - [CMake Options](#cmake-options)
- [SSZ Types](#ssz-types)
    - [Attestation](#attestation)
    - [AttestationData](#attestationdata)
    - [AttesterSlashing](#attesterslashing)
    - [BeaconBlock](#beaconblock)
    - [BeaconBlockBody](#beaconblockbody)
    - [BeaconBlockHeader](#beaconblockheader)
    - [BlockHashProof](#blockhashproof)
    - [BlsToExecutionChange](#blstoexecutionchange)
    - [C4Request](#c4request)
    - [Checkpoint](#checkpoint)
    - [Deposit](#deposit)
    - [DepositData](#depositdata)
    - [Eth1Data](#eth1data)
    - [EthAccountProof](#ethaccountproof)
    - [EthStateProof](#ethstateproof)
    - [EthStorageProof](#ethstorageproof)
    - [ExecutionPayload](#executionpayload)
    - [ExecutionPayloadHeader](#executionpayloadheader)
    - [ForkData](#forkdata)
    - [IndexAttestation](#indexattestation)
    - [LightClientHeader](#lightclientheader)
    - [LightClientUpdate](#lightclientupdate)
    - [ProposerSlashing](#proposerslashing)
    - [SignedBeaconBlock](#signedbeaconblock)
    - [SignedBeaconBlockheader](#signedbeaconblockheader)
    - [SignedBlsToExecutionChange](#signedblstoexecutionchange)
    - [SignedVoluntaryExit](#signedvoluntaryexit)
    - [SigningData](#signingdata)
    - [SyncAggregate](#syncaggregate)
    - [SyncCommittee](#synccommittee)
    - [SyncState](#syncstate)
    - [VoluntaryExit](#voluntaryexit)
    - [Withdrawal](#withdrawal)
- [License](#license)


## Concept

```mermaid
flowchart
    P[Proofer] --> V[Verifier]
    V --> P
    P -.-> RPC
```

The idea behind C4 is to create a ultra light client or better verifier which can be used in Websites, Mobile applications, but especially in embedded systems. The Proofer is a library which can used within you mobile app or in the backend to create Proof that the given data is valid. The Verifier is a library which can be used within the embedded system to verify this Proof.

The verifier itself is almost stateless and only needs to store the state of the sync committee, which changes every 27h. But with the latest sync committee the verifier is able to verify any proof with the signatures matching the previously verified public keys of the sync committee.
This allows independent Verification and security on any devices without the need to process every blockheader (as light clients usually would do).

### Updating the sync committee
 
A Sync Committee holds 512 validators signing every block. Every 27h the validators are updated. Since the verifier is passive, it will not activly sync. So whenever a proof is presented requiring a newer sync committee than the last state, it will tell it as part of the response to the proofer. The Proofer will then fetch the LightClientUpdates from any Beacon Chain API and push them to the verifier along with the Proof-Request. The verifier will then verify the LightClientUpdates and update the sync committee stored.

The Data in the [LightClientUpdate](#lightclientupdate) is used to verify the transition between two periods of the SyncCommittee. This is done oin 2 Steps:

1. calculate the SigningRoot by calculating the hash_tree_root of the new validator pubkeys and following the merkle proof down to the blockBodyRoot hash adn finally the SigningRoot
2. verify the BLS signature against the Signing Root as message and the aggregated pubkey of the old sync committee 

The following diagram shows the Structure of the Merkle Tree leading to the SigningRoot:

```mermaid
flowchart BT

    classDef noBorder fill:none,stroke:none;
    subgraph "header"
        Slot
        proposerIndex
        parentRoot
        stateRoot
        bodyRoot
    end

   subgraph "SigningData"
        blockheaderhash
        Domain
    end

   subgraph "BeaconState"
        beacon_mode(" ... ")
        current_sync_committee
        next_sync_committee
        inactivity_scores
        finalized_checkpoint

    end
    class beacon_mode noBorder

    subgraph "SyncCommittee"
        pubkeys
        aggregate_pubkey
    end

    subgraph "ValidatorPubKeys"
        Val1["Val 1"]
        Val1_a["[0..31]"]
        Val1_b["[32..64]"]
        Val2["Val 2"]
        Val2_a["[0..31]"]
        Val2_b["[32..48]"]
        val_mode(" ... ")
    end

    class val_mode noBorder

    blockheaderhash --> SigningRoot
    Domain --> SigningRoot
    4{4} --> blockheaderhash
    5{5} --> blockheaderhash
    8{8} --> 4
    9{9} --> 4
    10{10} --> 5
    11{11} --> 5
    Slot --> 8
    proposerIndex --> 8
    parentRoot --> 9
    stateRoot --> 9
    bodyRoot --> 10
    21{"zero"} --> 10
    22{"zero"} --> 11
    23{"zero"} --> 11


    38{38} --> stateRoot
    39{39} --> stateRoot

    76{76} --> 38
    77{77} --> 38
    78{78} --> 39
    79{79} --> 39

    156{156} -->78
    157{157} -->78

    158("...") --> 79

    314{314} --> 157
    315{315} --> 157

    finalized_checkpoint --> 314
    inactivity_scores --> 314
    current_sync_committee --> 315
    next_sync_committee --> 315


    pubkeys --> next_sync_committee
    aggregate_pubkey --> next_sync_committee

    2524{2524} --> pubkeys
    2525{2525} --> pubkeys


    5048{5048}  --> 2524
    5049{5049}  --> 2524
    10096{10096}  --> 5048
    10097{10097}  --> 5048
    20192{20192}  --> 10096
    20193{20193}  --> 10096
    40384{40384}  --> 20192
    40385{40385}  --> 20192
    80768{80768}  --> 40384
    80769{80769}  --> 40384
    161536{161536}  --> 80768
    161537{161537}  --> 80768
    323072{323072}  --> 161536
    323073{323073}  --> 161536
    Val1  --> 323072
    Val2  --> 323072

    Val1_a --> Val1
    Val1_b --> Val1
    Val2_a --> Val2
    Val2_b --> Val2


    class 158 noBorder

```

In order to validate, we need to calculate
- 512 x sha256 for each pubkey 
- 512 x sha256 merkle proof for the pubkeys
- 2 x sha256 for the SyncCommittee
- 5 x sha256 for the stateRoot
- 3 x sha256 for the blockheader hash
- 1 x for the SigningRoot

So in total, we need to verify 1035 hashes and 1 bls signature.


### Proof Requests

All requests send to the verifier are encoded using SSZ. The request itself is sepcified by the [C4Request](#c4request) type. This objects suports different types as data or proofs.

## Building


```sh
#clone
git clone https://github.com/colibri-labs/c4.git
cd c4

#build
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

#run 
bin/verify ../test/data/proof_data.ssz

```

### CMake Options

- **-DCLI**: generates the CLI tools.  
    Default: ON  
    Usage: `cmake -DCLI=ON` ..

- **-DCMAKE_BUILD_TYPE**: Build type  
    Default: Release  
    Options: Debug, Release, RelWithDebInfo, MinSizeRel  
    Usage: `cmake -DCMAKE_BUILD_TYPE=Release` ..

- **-DFILE_STORAGE**: if activated the verfifier will use a simple file-implementaion to store states in the current folder or in a folder specified by the env varC4_STATE_DIR  
    Default: ON  
    Usage: `cmake -DFILE_STORAGE=ON` ..

- **-DMESSAGES**: if activated the binaries will contain error messages, but for embedded systems this is not needed and can be turned off to save memory  
    Default: ON  
    Usage: `cmake -DMESSAGES=ON` ..

- **-DPRECOMPILE_ZERO_HASHES**: if activated zero hashes are cached which costs up to 1kb in RAM, but are needed in order to calc BeaconBodys in the proofer, but not in the client.in  
    Default: ON  
    Usage: `cmake -DPRECOMPILE_ZERO_HASHES=ON` ..


## SSZ Types

### Attestation


 The Type is defined in [proofer/ssz_types.c](https://github.com/corpus-core/c4/blob/main/src/proofer/ssz_types.c#L60).

```python
class Attestation(Container):
    aggregationBits: BitList [2048]
    data           : AttestationData
    signature      : ByteVector [96]
```

### AttestationData


 The Type is defined in [proofer/ssz_types.c](https://github.com/corpus-core/c4/blob/main/src/proofer/ssz_types.c#L22).

```python
class AttestationData(Container):
    slot           : Uint64
    index          : Uint64
    beaconBlockRoot: Bytes32
    source         : Checkpoint
    target         : Checkpoint
```

### AttesterSlashing


 The Type is defined in [proofer/ssz_types.c](https://github.com/corpus-core/c4/blob/main/src/proofer/ssz_types.c#L49).

```python
class AttesterSlashing(Container):
    signedHeader1: IndexAttestation
    signedHeader2: IndexAttestation
```

### BeaconBlock


 The Type is defined in [proofer/ssz_types.c](https://github.com/corpus-core/c4/blob/main/src/proofer/ssz_types.c#L154).

```python
class BeaconBlock(Container):
    slot         : Uint64    # the slot of the block or blocknumber
    proposerIndex: Uint64    # the index of the validator proposing the block
    parentRoot   : Bytes32   # the hash_tree_root of the parent block header
    stateRoot    : Bytes32   # the hash_tree_root of the state at the end of the block
    body         : BeaconBlockBody
```

### BeaconBlockBody

const ssz_def_t ssz_bls_pubky[] = {


 The Type is defined in [proofer/ssz_types.c](https://github.com/corpus-core/c4/blob/main/src/proofer/ssz_types.c#L139).

```python
class BeaconBlockBody(Container):
    randaoReveal         : ByteVector [96]
    eth1Data             : Eth1Data
    graffiti             : Bytes32
    proposerSlashings    : List [ProposerSlashing, MAX_PROPOSER_SLASHINGS]
    attesterSlashings    : List [AttesterSlashing, MAX_ATTESTER_SLASHINGS]
    attestations         : List [Attestation, MAX_ATTESTATIONS]
    deposits             : List [Deposit, MAX_DEPOSITS]
    voluntaryExits       : List [SignedVoluntaryExit, MAX_VOLUNTARY_EXITS]
    syncAggregate        : SyncAggregate
    executionPayload     : ExecutionPayload
    blsToExecutionChanges: List [SignedBlsToExecutionChange, MAX_BLS_TO_EXECUTION_CHANGES]
    blobKzgCommitments   : List [blsPubky, 4096]
```

### BeaconBlockHeader

the header of a beacon block


 The Type is defined in [verifier/types_beacon.c](https://github.com/corpus-core/c4/blob/main/src/verifier/types_beacon.c#L8).

```python
class BeaconBlockHeader(Container):
    slot         : Uint64    # the slot of the block or blocknumber
    proposerIndex: Uint64    # the index of the validator proposing the block
    parentRoot   : Bytes32   # the hash_tree_root of the parent block header
    stateRoot    : Bytes32   # the hash_tree_root of the state at the end of the block
    bodyRoot     : Bytes32   # the hash_tree_root of the block body
```

### BlockHashProof

the block hash proof is used as part of different other types since it contains all relevant
 proofs to validate the blockhash of the execution layer


 The Type is defined in [verifier/types_verify.c](https://github.com/corpus-core/c4/blob/main/src/verifier/types_verify.c#L10).

```python
class BlockHashProof(Container):
    blockhash_proof         : List [bytes32, 256]   # the merkle prooof from the executionPayload.blockhash down to the blockBodyRoot hash
    header                  : BeaconBlockHeader     # the header of the beacon block
    sync_committee_bits     : BitVector [512]       # the bits of the validators that signed the block
    sync_committee_signature: ByteVector [96]       # the signature of the sync committee
```

### BlsToExecutionChange


 The Type is defined in [proofer/ssz_types.c](https://github.com/corpus-core/c4/blob/main/src/proofer/ssz_types.c#L119).

```python
class BlsToExecutionChange(Container):
    validatorIndex    : Uint64
    fromBlsPubkey     : ByteVector [48]
    toExecutionAddress: Address
```

### C4Request

the main container defining the incoming data processed by the verifier


 The Type is defined in [verifier/types_verify.c](https://github.com/corpus-core/c4/blob/main/src/verifier/types_verify.c#L63).

```python
class C4Request(Container):
    data     : Union[                 # the data to proof
        None,
        Bytes32                       # the blochash  which is used for blockhash proof
    ]
    proof    : Union[                 # the proof of the data
        None,
        BlockHashProof,
        EthAccountProof               # the blockhash proof used validating blockhashes
    ]
    sync_data: Union[                 # the sync data containing proofs for the transition between the two periods
        None,
        List [LightClientUpdate, 512] # this light client update can be fetched directly from the beacon chain API
    ]
```

### Checkpoint


 The Type is defined in [proofer/ssz_types.c](https://github.com/corpus-core/c4/blob/main/src/proofer/ssz_types.c#L18).

```python
class Checkpoint(Container):
    epoch: Uint64
    root : Bytes32
```

### Deposit


 The Type is defined in [proofer/ssz_types.c](https://github.com/corpus-core/c4/blob/main/src/proofer/ssz_types.c#L74).

```python
class Deposit(Container):
    proof: Vector [bytes32, 33]
    data : DepositData
```

### DepositData


 The Type is defined in [proofer/ssz_types.c](https://github.com/corpus-core/c4/blob/main/src/proofer/ssz_types.c#L67).

```python
class DepositData(Container):
    pubkey               : ByteVector [48]
    withdrawalCredentials: Bytes32
    amount               : Uint64
    signature            : ByteVector [96]
```

### Eth1Data


 The Type is defined in [proofer/ssz_types.c](https://github.com/corpus-core/c4/blob/main/src/proofer/ssz_types.c#L54).

```python
class Eth1Data(Container):
    depositRoot : Bytes32
    depositCount: Uint64
    blockHash   : Bytes32
```

### EthAccountProof

represents the account and storage values, including the Merkle proof, of the specified account.


 The Type is defined in [verifier/types_verify.c](https://github.com/corpus-core/c4/blob/main/src/verifier/types_verify.c#L34).

```python
class EthAccountProof(Container):
    accountProof: List [bytes32, 256]           # Patricia merkle proof
    address     : Address                       # the address of the account
    balance     : Uint256                       # the balance of the account
    codeHash    : Bytes32                       # the code hash of the account
    nonce       : Uint256                       # the nonce of the account
    storageHash : Bytes32                       # the storage hash of the account
    storageProof: List [EthStorageProof, 256]   # the storage proofs of the selected
    state_proof : EthStateProof                 # the state proof of the account
```

### EthStateProof

the stateRoot proof is used as part of different other types since it contains all relevant
 proofs to validate the stateRoot of the execution layer


 The Type is defined in [verifier/types_verify.c](https://github.com/corpus-core/c4/blob/main/src/verifier/types_verify.c#L18).

```python
class EthStateProof(Container):
    state_proof             : List [bytes32, 256]   # the merkle prooof from the executionPayload.state down to the blockBodyRoot hash
    header                  : BeaconBlockHeader     # the header of the beacon block
    sync_committee_bits     : BitVector [512]       # the bits of the validators that signed the block
    sync_committee_signature: ByteVector [96]       # the signature of the sync committee
```

### EthStorageProof

represents the storage proof of a key


 The Type is defined in [verifier/types_verify.c](https://github.com/corpus-core/c4/blob/main/src/verifier/types_verify.c#L25).

```python
class EthStorageProof(Container):
    key  : Bytes32             # the key to be proven
    proof: List [bytes32, 5]   # Patricia merkle proof
    value: Bytes32
```

### ExecutionPayload

the block header of the execution layer proved within the beacon block


 The Type is defined in [proofer/ssz_types.c](https://github.com/corpus-core/c4/blob/main/src/proofer/ssz_types.c#L100).

```python
class ExecutionPayload(Container):
    parentHash   : Bytes32                             # the hash of the parent block
    feeRecipient : Address                             # the address of the fee recipient
    stateRoot    : Bytes32                             # the merkle root of the state at the end of the block
    receiptsRoot : Bytes32                             # the merkle root of the transactionreceipts
    logsBloom    : ByteVector [256]                    # the bloom filter of the logs
    prevRandao   : Bytes32                             # the randao of the previous block
    blockNumber  : Uint64                              # the block number
    gasLimit     : Uint64                              # the gas limit of the block
    gasUsed      : Uint64                              # the gas used of the block
    timestamp    : Uint64                              # the timestamp of the block
    extraData    : Bytes[32]                           # the extra data of the block
    baseFeePerGas: Uint256                             # the base fee per gas of the block
    blockHash    : Bytes32                             # the hash of the block
    transactions : List [transactionsBytes, 1048576]   # the list of transactions
    withdrawals  : List [Withdrawal, 16]               # the list of withdrawels
    blobGasUsed  : Uint64                              # the gas used for the blob transactions
    excessBlobGas: Uint64                              # the excess blob gas of the block
```

### ExecutionPayloadHeader

the block header of the execution layer proved within the beacon block


 The Type is defined in [verifier/types_beacon.c](https://github.com/corpus-core/c4/blob/main/src/verifier/types_beacon.c#L21).

```python
class ExecutionPayloadHeader(Container):
    parentHash      : Bytes32            # the hash of the parent block
    feeRecipient    : Address            # the address of the fee recipient
    stateRoot       : Bytes32            # the merkle root of the state at the end of the block
    receiptsRoot    : Bytes32            # the merkle root of the transactionreceipts
    logsBloom       : ByteVector [256]   # the bloom filter of the logs
    prevRandao      : Bytes32            # the randao of the previous block
    blockNumber     : Uint64             # the block number
    gasLimit        : Uint64             # the gas limit of the block
    gasUsed         : Uint64             # the gas used of the block
    timestamp       : Uint64             # the timestamp of the block
    extraData       : Bytes[32]          # the extra data of the block
    baseFeePerGas   : Uint256            # the base fee per gas of the block
    blockHash       : Bytes32            # the hash of the block
    transactionsRoot: Bytes32            # the merkle root of the transactions
    withdrawalsRoot : Bytes32            # the merkle root of the withdrawals
    blobGasUsed     : Uint64             # the gas used for the blob transactions
    excessBlobGas   : Uint64             # the excess blob gas of the block
```

### ForkData

the fork data is used to create the domain


 The Type is defined in [verifier/verify_blockhash_proof.c](https://github.com/corpus-core/c4/blob/main/src/verifier/verify_blockhash_proof.c#L27).

```python
class ForkData(Container):
    version: ByteVector [4]   # the version of the fork
    state  : Bytes32          # the state of the Genisis Block
```

### IndexAttestation


 The Type is defined in [proofer/ssz_types.c](https://github.com/corpus-core/c4/blob/main/src/proofer/ssz_types.c#L32).

```python
class IndexAttestation(Container):
    attestingIndices: List [uint8, 2048]
    data            : AttestationData
    signature       : ByteVector [96]
```

### LightClientHeader

the header of the light client update


 The Type is defined in [verifier/types_beacon.c](https://github.com/corpus-core/c4/blob/main/src/verifier/types_beacon.c#L46).

```python
class LightClientHeader(Container):
    beacon         : BeaconBlockHeader        # the header of the beacon block
    execution      : ExecutionPayloadHeader   # the header of the execution layer proved within the beacon block
    executionBranch: Vector [bytes32, 4]      # the merkle proof of the execution layer proved within the beacon block
```

### LightClientUpdate

the light client update is used to verify the transition between two periods of the SyncCommittee.
 This data will be fetched directly through the beacon Chain API since it contains all required data.


 The Type is defined in [verifier/types_beacon.c](https://github.com/corpus-core/c4/blob/main/src/verifier/types_beacon.c#L53).

```python
class LightClientUpdate(Container):
    attestedHeader         : LightClientHeader     # the header of the beacon block attested by the sync committee
    nextSyncCommittee      : SyncCommittee
    nextSyncCommitteeBranch: Vector [bytes32, 5]   # will be 6 in electra
    finalizedHeader        : LightClientHeader     # the header of the finalized beacon block
    finalityBranch         : Vector [bytes32, 6]   # will be 7 in electra
    syncAggregate          : SyncAggregate         # the aggregates signature of the sync committee
    signatureSlot          : Uint64                # the slot of the signature
```

### ProposerSlashing


 The Type is defined in [proofer/ssz_types.c](https://github.com/corpus-core/c4/blob/main/src/proofer/ssz_types.c#L44).

```python
class ProposerSlashing(Container):
    signedHeader1: SignedBeaconBlockheader
    signedHeader2: SignedBeaconBlockheader
```

### SignedBeaconBlock


 The Type is defined in [proofer/ssz_types.c](https://github.com/corpus-core/c4/blob/main/src/proofer/ssz_types.c#L161).

```python
class SignedBeaconBlock(Container):
    message  : BeaconBlock
    signature: ByteVector [96]
```

### SignedBeaconBlockheader


 The Type is defined in [proofer/ssz_types.c](https://github.com/corpus-core/c4/blob/main/src/proofer/ssz_types.c#L40).

```python
class SignedBeaconBlockheader(Container):
    message  : BeaconBlockHeader
    signature: ByteVector [96]
```

### SignedBlsToExecutionChange


 The Type is defined in [proofer/ssz_types.c](https://github.com/corpus-core/c4/blob/main/src/proofer/ssz_types.c#L125).

```python
class SignedBlsToExecutionChange(Container):
    message  : BlsToExecutionChange
    signature: ByteVector [96]
```

### SignedVoluntaryExit


 The Type is defined in [proofer/ssz_types.c](https://github.com/corpus-core/c4/blob/main/src/proofer/ssz_types.c#L84).

```python
class SignedVoluntaryExit(Container):
    message  : VoluntaryExit
    signature: ByteVector [96]
```

### SigningData

combining the root with a domain to ensure uniqueness of the signing message


 The Type is defined in [verifier/verify_blockhash_proof.c](https://github.com/corpus-core/c4/blob/main/src/verifier/verify_blockhash_proof.c#L20).

```python
class SigningData(Container):
    root  : Bytes32   # the hashed root of the data to sign
    domain: Bytes32   # the domain of the data to sign
```

### SyncAggregate

the aggregates signature of the sync committee


 The Type is defined in [verifier/types_beacon.c](https://github.com/corpus-core/c4/blob/main/src/verifier/types_beacon.c#L41).

```python
class SyncAggregate(Container):
    syncCommitteeBits     : BitVector [512]   # the bits of the validators that signed the block (each bit represents a validator)
    syncCommitteeSignature: ByteVector [96]   # the signature of the sync committee
```

### SyncCommittee

the public keys sync committee used within a period ( about 27h)


 The Type is defined in [verifier/types_beacon.c](https://github.com/corpus-core/c4/blob/main/src/verifier/types_beacon.c#L16).

```python
class SyncCommittee(Container):
    pubkeys        : Vector [blsPubky, 512]   # the 512 pubkeys (each 48 bytes) of the validators in the sync committee
    aggregatePubkey: ByteVector [48]          # the aggregate pubkey (48 bytes) of the sync committee
```

### SyncState

the sync state of the sync committee. This is used to store the verfied validators as state within the verifier.


 The Type is defined in [verifier/sync_committee.c](https://github.com/corpus-core/c4/blob/main/src/verifier/sync_committee.c#L13).

```python
class SyncState(Container):
    validators: Vector [blsPubky, 512]   # the list of the validators
    period    : Uint32                   # the period of the sync committee
```

### VoluntaryExit


 The Type is defined in [proofer/ssz_types.c](https://github.com/corpus-core/c4/blob/main/src/proofer/ssz_types.c#L79).

```python
class VoluntaryExit(Container):
    epoch         : Uint64
    validatorIndex: Uint64
```

### Withdrawal


 The Type is defined in [proofer/ssz_types.c](https://github.com/corpus-core/c4/blob/main/src/proofer/ssz_types.c#L89).

```python
class Withdrawal(Container):
    index         : Uint64
    validatorIndex: Uint64
    address       : Address
    amount        : Uint64
```
## License

MIT