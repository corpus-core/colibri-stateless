# C4 (corpus core colibri client)

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

## SSZ Types

### BeaconBlockHeader

```python
class BeaconBlockHeader(Container):
slot: Uint64
proposerIndex: Uint64
parentRoot: Bytes32
stateRoot: Bytes32
bodyRoot: Bytes32
```

### SyncCommittee

```python
class SyncCommittee(Container):
pubkeys: Vector [blsPubky, 512]   # [blsPubky](#blspubky)
aggregatePubkey: ByteVector
```

### ExecutionPayloadHeader

```python
class ExecutionPayloadHeader(Container):
parentHash: Bytes32
feeRecipient: Address
stateRoot: Bytes32
receiptsRoot: Bytes32
logsBloom: ByteVector
prevRandao: Bytes32
blockNumber: Uint64
gasLimit: Uint64
gasUsed: Uint64
timestamp: Uint64
extraData: Bytes
baseFeePerGas: Uint256
blockHash: Bytes32
transactionsRoot: Bytes32
withdrawalsRoot: Bytes32
blobGasUsed: Uint64
excessBlobGas: Uint64
```

### SyncAggregate

```python
class SyncAggregate(Container):
syncCommitteeBits: BitVector [512]
syncCommitteeSignature: ByteVector
```

### LightClientHeader

```python
class LightClientHeader(Container):
beacon: BeaconBlockHeader   # [BeaconBlockHeader](#beaconblockheader)
execution: ExecutionPayloadHeader   # [ExecutionPayloadHeader](#executionpayloadheader)
executionBranch: Vector [bytes32, 4]   # [bytes32](#bytes32)
```

### LightClientUpdate

```python
class LightClientUpdate(Container):
attestedHeader: LightClientHeader   # [LightClientHeader](#lightclientheader)
nextSyncCommittee: SyncCommittee   # [SyncCommittee](#synccommittee)
nextSyncCommitteeBranch: Vector [bytes32, 5]   # [bytes32](#bytes32)
finalizedHeader: LightClientHeader   # [LightClientHeader](#lightclientheader)
finalityBranch: Vector [bytes32, 6]   # [bytes32](#bytes32)
syncAggregate: SyncAggregate   # [SyncAggregate](#syncaggregate)
signatureSlot: Uint64
```

### BlockHashProof

```python
class BlockHashProof(Container):
blockhash_proof: List [bytes32, 256]   # [bytes32](#bytes32)
header: BeaconBlockHeader   # [BeaconBlockHeader](#beaconblockheader)
sync_committee_bits: BitVector [512]
sync_committee_signature: ByteVector
```

### Union C4RequestData

```python
C4RequestData = Union [ None , Bytes32]
```

### Union C4RequestProofs

```python
C4RequestProofs = Union [ None , BlockHashProof]
```

### Union C4RequestSyncdata

```python
C4RequestSyncdata = Union [ None , List [LightClientUpdate, 512]]
```

### C4Request

```python
class C4Request(Container):
data: C4RequestData <union>    # [C4RequestData](#union-c4requestdata)
proof: C4RequestProofs <union>    # [C4RequestProofs](#union-c4requestproofs)
sync_data: C4RequestSyncdata <union>    # [C4RequestSyncdata](#union-c4requestsyncdata)
```
## License

MIT