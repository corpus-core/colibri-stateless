<img src="c4_logo.png" alt="C4 Logo" width="300"/>

# C4 (corpus core colibri client)

![ETH2.0_Spec_Version 1.4.0](https://img.shields.io/badge/ETH2.0_Spec_Version-1.4.0-2e86c1.svg)
[![CI on multiple platforms](https://github.com/corpus-core/c4/actions/workflows/cmake-multi-platform.yml/badge.svg)](https://github.com/corpus-core/c4/actions/workflows/cmake-multi-platform.yml)
## Index

- [Index](#index)
- [Concept](#concept)
    - [Updating the sync committee](#updating-the-sync-committee)
    - [Verifying Blockhashes](#verifying-blockhashes)
    - [Merkle Proofs](#merkle-proofs)
        - [Patricia Merkle Proof](#patricia-merkle-proof)
        - [SSZ Merkle Proof](#ssz-merkle-proof)
- [RPC Proofs](#rpc-proofs)
- [Building](#building)
    - [Build Javascript bindings (WASM)](#build-javascript-bindings-(wasm))
    - [CMake Options](#cmake-options)
        - [general options](#general-options)
        - [eth options](#eth-options)
        - [util options](#util-options)
    - [Running on embedded devices](#running-on-embedded-devices)
        - [Embedded Tests](#embedded-tests)
        - [CI Workflows](#ci-workflows)
- [SSZ Types](#ssz-types)
    - [Beacon Types](#beacon-types)
        - [Attestation](#attestation)
        - [AttestationData](#attestationdata)
        - [AttesterSlashing](#attesterslashing)
        - [BeaconBlock](#beaconblock)
        - [BeaconBlockBody](#beaconblockbody)
        - [BeaconBlockHeader](#beaconblockheader)
        - [BlsToExecutionChange](#blstoexecutionchange)
        - [Checkpoint](#checkpoint)
        - [DenepExecutionPayload](#denepexecutionpayload)
        - [Deposit](#deposit)
        - [DepositData](#depositdata)
        - [Eth1Data](#eth1data)
        - [ExecutionPayloadHeader](#executionpayloadheader)
        - [IndexAttestation](#indexattestation)
        - [LightClientHeader](#lightclientheader)
        - [LightClientUpdate](#lightclientupdate)
        - [ProposerSlashing](#proposerslashing)
        - [SignedBeaconBlock](#signedbeaconblock)
        - [SignedBeaconBlockheader](#signedbeaconblockheader)
        - [SignedBlsToExecutionChange](#signedblstoexecutionchange)
        - [SignedVoluntaryExit](#signedvoluntaryexit)
        - [SyncAggregate](#syncaggregate)
        - [SyncCommittee](#synccommittee)
        - [VoluntaryExit](#voluntaryexit)
        - [Withdrawal](#withdrawal)
    - [C4 ETH Request](#c4-eth-request)
        - [C4Request](#c4request)
    - [Proof Types](#proof-types)
        - [EthAccountProof](#ethaccountproof)
        - [EthBlockProof](#ethblockproof)
        - [EthCallAccount](#ethcallaccount)
        - [EthCallProof](#ethcallproof)
        - [EthLogsBlock](#ethlogsblock)
        - [EthLogsTx](#ethlogstx)
        - [EthReceiptProof](#ethreceiptproof)
        - [EthStateBlockEnum](#ethstateblockenum)
        - [EthStateProof](#ethstateproof)
        - [EthStorageProof](#ethstorageproof)
        - [EthSyncProof](#ethsyncproof)
        - [EthTransactionProof](#ethtransactionproof)
    - [Data Types](#data-types)
        - [EthAccessListData](#ethaccesslistdata)
        - [EthBlockData](#ethblockdata)
        - [EthBlockDataTransactionUntion](#ethblockdatatransactionuntion)
        - [EthProofData](#ethproofdata)
        - [EthReceiptData](#ethreceiptdata)
        - [EthReceiptDataLog](#ethreceiptdatalog)
        - [EthStorageProofData](#ethstorageproofdata)
        - [EthTxData](#ethtxdata)
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


### Verifying Blockhashes

Blocks in the execution layer are always verified by the blocks in the consensus layer. Each BeaconBlock has a executionPayload which holds the data of the Block in the execution layer. So many information like transactions, blockNumber and blockHash can directly be verified with the beacon block, while others like the receipts or the stateRoot, still needs Patricia Merkle Proofs from the execution layer to be verified.

But the most ciritcal verification is checking that a BeaconBlock is valid. This is done by using the SyncCommittee, which holds 512 public keys of validators and change very period (about every 27 hours). Having the correct keys is critical to verify the blockhash. 
Now the C4 Client is always trying to hold the SyncCommittees public keys up to date, having the latest sync committee so we can checking the aggregated BLS Signature of them to verify blockhashes.

But what happens, if you want to verify a blockhash of an older block, because you need an older transaction?
In this case the you can use a new BeaconState and and create a MerkleProof for the historical roots. Since this is a list, there is practiclly no limit ( https://ethereum.github.io/consensus-specs/specs/phase0/beacon-chain/#state-list-lengths  HISTORICAL_ROOTS_LIMIT = 2 ** 40 = 52262 years).
Since the capella Fork the state contains the [HistoricalSuummary](https://ethereum.github.io/consensus-specs/specs/capella/beacon-chain/#historicalsummary) which holds all the bloclhashes and state roots, so you can create a proof for the historical roots.
Unfortunatly the current specification for the [Beacon API](https://ethereum.github.io/beacon-APIs/) does not support providing proofs for those data yet, but we are planning to create an EIP to change this.


## RPC Proofs

All requests send to the verifier are encoded using SSZ. The request itself is sepcified by the [C4Request](#c4request) type. This objects suports different types as data or proofs.

In order to proof the RPC-Request, the  proofer will use different proofs.


| rpc-Method                                                                                | status | Data                                             | Proof                                           |
| :---------------------------------------------------------------------------------------- | :----- | :----------------------------------------------- | :---------------------------------------------- |
| [`eth_blockNumber`](https://docs.alchemy.com/reference/eth-blocknumber)                   | ✖️     | Uint64                                           |                    |
| [`eth_feeHistorry`](https://docs.alchemy.com/reference/eth-feehistory)                    | ✖️     |                                             |                    |
| [`eth_chainId`](https://docs.alchemy.com/reference/eth-chainid)                           | ✅     | Uint64                                           |                    |
| [`eth_accounts`](https://docs.alchemy.com/reference/eth-accounts)                         | ✅     | [address]]                                       |                    |
| `eth_blobBaseFee`                                                                         | ✖️     | Uint64                                           | [EthBlockHeaderProof](#ethblockheaderproof)     |
| [`eth_call`](https://docs.alchemy.com/reference/eth-call)                                 | ✅     | Bytes                                            | [EthCallProof](#ethcallproof)                   |
| [`eth_createAccessList`](https://docs.alchemy.com/reference/eth-createaccesslist)         | ✖️     | [EthAccessData](#ethaccessdata)                  | [EthCallProof](#ethcallproof)                   |
| [`eth_estimateGas`](https://docs.alchemy.com/reference/eth-estimategas)                   | ✖️     | Uint64                                           | [EthCallProof](#ethcallproof)                   |
| [`eth_feeHistory`](https://docs.alchemy.com/reference/eth-feehistory)                     | ✖️     |                                                  |                                                 |
| [`eth_gasPrice`](https://docs.alchemy.com/reference/eth-gasprice)                         | ✖️     |                                                  |                                                 |
| [`eth_getBalance`](https://docs.alchemy.com/reference/eth-getbalance)                     | ✅     | Uint256                                          | [EthAccountProof](#ethaccountproof)             |
| [`eth_getBlockByHash`](https://docs.alchemy.com/reference/eth-getblockbyhash)             | ✅     | [EthBlockData](#ethblockdata)                    | [EthBlockProof](#ethblockproof)                 |
| [`eth_getBlockByNumber`](https://docs.alchemy.com/reference/eth-getblockbynumber)         | ✅     | [EthBlockData](#ethblockdata)                    | [EthBlockProof](#ethblockproof)                 |
| [`eth_getBlockReceipts`](https://docs.alchemy.com/reference/eth-getblockreceipts)         | ✖️     |                                                  |                                                 |
| [`eth_getBlockTransactionCountByHash`](https://docs.alchemy.com/reference/eth-getblocktransactioncountbyhash) | ✖️     |                                                  |                                                 |
| [`eth_getBlockTransactionCountByNumber`](https://docs.alchemy.com/reference/eth-getblocktransactioncountbynumber) | ✖️     |                                                  |                                                 |
| [`eth_getCode`](https://docs.alchemy.com/reference/eth-getcode)                           | ✅     | Bytes                                            | [EthAccountProof](#ethaccountproof)             |
| [`eth_newPendingTransactionFilter`](https://docs.alchemy.com/reference/eth-newpendingtransactionfilter)            | ✖️     |                                                  |                                                 |
| [`eth_newFilter`](https://docs.alchemy.com/reference/eth-newfilter)            | ✖️     |                                                  |                                                 |
| [`eth_newBlockFilter`](https://docs.alchemy.com/reference/eth-newblockfilter)            | ✖️     |                                                  |                                                 |
| [`eth_FilterChanges`](https://docs.alchemy.com/reference/eth-getfilterchanges)            | ✖️     |                                                  |                                                 |
| [`eth_FilterLogs`](https://docs.alchemy.com/reference/eth-getfilterlogs)                  | ✖️     |                                                  |                                                 |
| [`eth_uninstallFilter`](https://docs.alchemy.com/reference/eth-uninstallfilter)            | ✖️     |                                                  |                                                 |
| [`eth_subscribe`](https://docs.alchemy.com/reference/eth-subscribe)            | ✖️     |                                                  |                                                 |
| [`eth_unsubscribe`](https://docs.alchemy.com/reference/eth-unsubscribe)            | ✖️     |                                                  |                                                 |
| [`eth_getLogs`](https://docs.alchemy.com/reference/eth-getlogs)                           | ✅     | List<[EthReceiptDataLog](#ethreceiptdatalog)>    | List<[EthLogsBlock](#ethlogsblock)>             |
| [`eth_getTransactionCount`](https://docs.alchemy.com/reference/eth-gettransactioncount)   | ✅     | Uint256                                          | [EthAccountProof](#ethaccountproof)             |
| [`eth_getStorageAt`](https://docs.alchemy.com/reference/eth-getstorageat)                 | ✅     | Bytes32                                          | [EthAccountProof](#ethaccountproof)             |
| [`eth_getProof`](https://docs.alchemy.com/reference/eth-getproof)                         | ✅     | [EthProofData](#ethproofdata)                   | [EthAccountProof](#ethaccountproof)             |
| [`eth_getTransactionReceipt`](https://docs.alchemy.com/reference/eth-gettransactionreceipt) | ✅     | [EthReceiptData](#ethreceiptdata)                | [EthReceiptProof](#ethreceiptproof)             |
| [`eth_getTransactionByHash`](https://docs.alchemy.com/reference/eth-gettransactionbyhash) | ✅     | [EthTransactionData](#ethtransactiondata)        | [EthTransactionProof](#ethtransactionproof)     |
| [`eth_getTransactionByBlockHashAndIndex`](https://docs.alchemy.com/reference/eth-gettransactionbyblockhashandindex) | ✅     | [EthTransactionData](#ethtransactiondata)        | [EthTransactionProof](#ethtransactionproof)     |
| [`eth_getTransactionByBlockNumberAndIndex`](https://docs.alchemy.com/reference/eth-gettransactionbyblocknumberandindex) | ✅     | [EthTransactionData](#ethtransactiondata)        | [EthTransactionProof](#ethtransactionproof)     |
| [`eth_getUncleByBlockHash`](https://docs.alchemy.com/reference/eth-getunclesbyblockhashandindex)   | ✅     |                                                  |                                                 |
| [`eth_getUncleByBlockNumber`](https://docs.alchemy.com/reference/eth-getunclesbyblocknumberandindex)   | ✅     |                                                  |                                                 |
| [`eth_getUncleCountByBlockHash`](https://docs.alchemy.com/reference/eth-getunclesbyblockhashandindex)   | ✅     |                                                  |                                                 |
| [`eth_getUncleCountByBlockNumber`](https://docs.alchemy.com/reference/eth-getunclesbyblocknumberandindex)   | ✅     |                                                  |                                                 |
| [`eth_maxPriorityFeePerGas`](https://docs.alchemy.com/reference/eth-maxpriorityfeepergas)   | ✖️     |                                                  |                                                 |
| [`eth_protocolVersion`](https://docs.alchemy.com/reference/eth-protocolversion)   | ✅     |  Uint256                                                |                                                 |
| [`eth_sendRawTransaction`](https://docs.alchemy.com/reference/eth-sendrawtransaction)   | ✖️     |                                                  |                                                 |
| [`web3_clientVersion`](https://docs.alchemy.com/reference/web3-clientversion)   | ✅     |                                                  |                                                 |
| [`web3_sha3`](https://docs.alchemy.com/reference/web3-sha3)   | ✅     | Bytes32                                      |                                                 |


web3_clientVersion



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

### Build Javascript bindings (WASM)

In order to build the Javascript bindings, you need to have [emscripten installed](https://emscripten.org/docs/getting_started/downloads.html). 

```sh
cd build
emcmake cmake -DWASM=true -DCURL=false ..
make
```
The js-module will be in the `build/emscripten` folder.


### CMake Options

#### general options 

| Flag | descr  | default |
| :--- | :----- | :----- |
| **BLS_DESERIALIZE** | Store BLS keys deserialized. It is faster but uses 25k more memory in cache per period. | ON  |
| **CHAIN_ETH** | includes the ETH verification support | ON  |
| **CLI** | Build command line tools | ON  |
| **CMAKE_BUILD_TYPE** | Build type (Debug, Release, RelWithDebInfo, MinSizeRel) | Release  |
| **COMBINED_STATIC_LIB** | Build a combined static library | OFF  |
| **COVERAGE** | Enable coverage tracking | OFF  |
| **CURL** | Enable CURL support | ON  |
| **EMBEDDED** | Build for embedded target | OFF  |
| **HTTP_SERVER** | Build the HTTP server using libuv and llhttp | OFF  |
| **INCLUDE** | Path to additional CMakeLists.txt Dir, which will included into the build, allowing to extend the binaries. |   |
| **KOTLIN** | Build Kotlin bindings | OFF  |
| **MESSAGES** | if activated the binaries will contain error messages, but for embedded systems this is not needed and can be turned off to save memory | ON  |
| **PROOFER** | Build the proofer library | ON  |
| **PROOFER_CACHE** | Caches blockhashes and maps, which makes a lot of sense on a server | OFF  |
| **SHAREDLIB** | Build shared library | OFF  |
| **STATIC_MEMORY** | if true, the memory will be statically allocated, which only makes sense for embedded systems | OFF  |
| **SWIFT** | Build Swift bindings | OFF  |
| **TEST** | Build the unit tests | OFF  |
| **VERIFIER** | Build the verifier library | ON  |
| **WASM** | Build WebAssembly target | OFF  |



#### eth options 

| Flag | descr  | default |
| :--- | :----- | :----- |
| **ETH_ACCOUNT** | support eth account verification. eth_getBalance, eth_getStorageAt, eth_getProof, eth_getCode, eth_getTransactionCount | ON  |
| **ETH_BLOCK** | support eth block verification. eth_getBlockByHash, eth_getBlockByNumber, eth_getBlockTransactionCountByHash, eth_getBlockTransactionCountByNumber, eth_getUncleCountByBlockHash, eth_getUncleCountByBlockNumber | ON  |
| **ETH_CALL** | support eth call verification. eth_call, eth_estimateGas | ON  |
| **ETH_LOGS** | support eth logs verification. eth_getLogs | ON  |
| **ETH_RECEIPT** | support eth receipt verification. eth_getTransactionReceipt | ON  |
| **ETH_TX** | support eth Transaction verification. eth_getTransactionByHash, eth_getTransactionByBlockHashAndIndex, eth_getTransactionByBlockNumberAndIndex | ON  |
| **ETH_UTIL** | support eth utils like eth_chainId or web3_sha3 | ON  |
| **EVMLIGHT** | uses evmlight vor eth_call verification, which is smaller and faster, but does not track gas. | OFF  |
| **EVMONE** | uses evmone to verify eth_calls | ON  |
| **PRECOMPILES_RIPEMD160** | Precompile ripemd160 | ON  |



#### util options 

| Flag | descr  | default |
| :--- | :----- | :----- |
| **FILE_STORAGE** | if activated the verfifier will use a simple file-implementaion to store states in the current folder or in a folder specified by the env varC4_STATE_DIR | ON  |
| **PRECOMPILE_ZERO_HASHES** | if activated zero hashes are cached which costs up to 1kb in RAM, but are needed in order to calc BeaconBodys in the proofer, but not in the verfier | ON  |


### Running on embedded devices

Colibri is designed to be portable and run on a variety of platforms, including embedded systems.

The C4 verifier can run on embedded systems with the following minimum specifications:

- **Flash/ROM**: ~225 KB (for code and read-only data)
- **RAM**: ~108 KB minimum, 128 KB recommended
- **CPU**: ARM Cortex-M4/M7 or more powerful processors

For more details on embedded systems support, see [test/embedded/README.md](test/embedded/README.md).


```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../test/embedded/toolchain.cmake -DEMBEDDED=ON -DCURL=OFF -DPROOFER=OFF -DCLI=OFF
make
```

#### Embedded Tests

```bash
# Run minimal verification test (requires QEMU)
../test/embedded/run_minimal_with_log.sh

# Run full verification test (requires QEMU)
../test/embedded/run_embedded_test.sh
```

#### CI Workflows

The project includes several GitHub Actions workflows:

- **Embedded Build Analysis**: Builds the C4 verifier for embedded targets and analyzes memory usage
- **Embedded Verification Test**: Tests the C4 verifier in an emulated environment (QEMU) to verify functionality
- **Standard Tests**: Runs the standard test suite on multiple platforms



## SSZ Types


### Beacon Types

The  SSZ types for the Beacon chain for the Denep Fork.

#### Attestation

an attestation is a list of aggregation bits, a data and a signature


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L105).

```python
class Attestation(Container):
    aggregationBits: BitList [2048]
    data           : AttestationData
    signature      : ByteVector [96]
```

#### AttestationData

the data of an attestation


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L64).

```python
class AttestationData(Container):
    slot           : Uint64       # the slot of the attestation
    index          : Uint64       # the index of the attestation
    beaconBlockRoot: Bytes32      # the root of the beacon block
    source         : Checkpoint   # the source of the attestation
    target         : Checkpoint   # the target of the attestation
```

#### AttesterSlashing

an attester slashing is a list of two index attestations


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L92).

```python
class AttesterSlashing(Container):
    signedHeader1: IndexAttestation
    signedHeader2: IndexAttestation
```

#### BeaconBlock


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L166).

```python
class BeaconBlock(Container):
    slot         : Uint64    # the slot of the block or blocknumber
    proposerIndex: Uint64    # the index of the validator proposing the block
    parentRoot   : Bytes32   # the hash_tree_root of the parent block header
    stateRoot    : Bytes32   # the hash_tree_root of the state at the end of the block
    body         : BeaconBlockBody
```

#### BeaconBlockBody


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L151).

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
    executionPayload     : DenepExecutionPayload
    blsToExecutionChanges: List [SignedBlsToExecutionChange, MAX_BLS_TO_EXECUTION_CHANGES]
    blobKzgCommitments   : List [blsPubky, 4096]
```

#### BeaconBlockHeader

the header of a beacon block


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L15).

```python
class BeaconBlockHeader(Container):
    slot         : Uint64    # the slot of the block or blocknumber
    proposerIndex: Uint64    # the index of the validator proposing the block
    parentRoot   : Bytes32   # the hash_tree_root of the parent block header
    stateRoot    : Bytes32   # the hash_tree_root of the state at the end of the block
    bodyRoot     : Bytes32   # the hash_tree_root of the block body
```

#### BlsToExecutionChange


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L133).

```python
class BlsToExecutionChange(Container):
    validatorIndex    : Uint64
    fromBlsPubkey     : ByteVector [48]
    toExecutionAddress: Address
```

#### Checkpoint

a checkpoint is a tuple of epoch and root


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L58).

```python
class Checkpoint(Container):
    epoch: Uint64    # the epoch of the checkpoint
    root : Bytes32   # the root of the checkpoint
```

#### DenepExecutionPayload

the block header of the execution layer proved within the beacon block


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L37).

```python
class DenepExecutionPayload(Container):
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
    withdrawals  : List [DenepWithdrawal, 16]          # the list of withdrawels
    blobGasUsed  : Uint64                              # the gas used for the blob transactions
    excessBlobGas: Uint64                              # the excess blob gas of the block
```

#### Deposit


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L118).

```python
class Deposit(Container):
    proof: Vector [bytes32, 33]
    data : DepositData
```

#### DepositData


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L111).

```python
class DepositData(Container):
    pubkey               : ByteVector [48]
    withdrawalCredentials: Bytes32
    amount               : Uint64
    signature            : ByteVector [96]
```

#### Eth1Data

the eth1 data is a deposit root, a deposit count and a block hash


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L98).

```python
class Eth1Data(Container):
    depositRoot : Bytes32
    depositCount: Uint64
    blockHash   : Bytes32
```

#### ExecutionPayloadHeader

the block header of the execution layer proved within the beacon block


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L190).

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

#### IndexAttestation

an index attestation is a list of attesting indices, a data and a signature


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L73).

```python
class IndexAttestation(Container):
    attestingIndices: List [uint8, 2048]   # the list of attesting indices
    data            : AttestationData      # the data of the attestation
    signature       : ByteVector [96]      # the BLS signature of the attestation
```

#### LightClientHeader

the header of the light client update


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L210).

```python
class LightClientHeader(Container):
    beacon         : BeaconBlockHeader        # the header of the beacon block
    execution      : ExecutionPayloadHeader   # the header of the execution layer proved within the beacon block
    executionBranch: Vector [bytes32, 4]      # the merkle proof of the execution layer proved within the beacon block
```

#### LightClientUpdate

the light client update is used to verify the transition between two periods of the SyncCommittee.
 This data will be fetched directly through the beacon Chain API since it contains all required data.


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L217).

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

#### ProposerSlashing

a proposer slashing is a list of two signed beacon block headers


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L86).

```python
class ProposerSlashing(Container):
    signedHeader1: SignedBeaconBlockheader
    signedHeader2: SignedBeaconBlockheader
```

#### SignedBeaconBlock


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L173).

```python
class SignedBeaconBlock(Container):
    message  : BeaconBlock
    signature: ByteVector [96]
```

#### SignedBeaconBlockheader

a signed beacon block header is a beacon block header and a signature


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L80).

```python
class SignedBeaconBlockheader(Container):
    message  : BeaconBlockHeader   # the beacon block header
    signature: ByteVector [96]     # the BLS signature of the beacon block header
```

#### SignedBlsToExecutionChange


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L139).

```python
class SignedBlsToExecutionChange(Container):
    message  : BlsToExecutionChange
    signature: ByteVector [96]
```

#### SignedVoluntaryExit


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L128).

```python
class SignedVoluntaryExit(Container):
    message  : VoluntaryExit
    signature: ByteVector [96]
```

#### SyncAggregate

the aggregates signature of the sync committee


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L23).

```python
class SyncAggregate(Container):
    syncCommitteeBits     : BitVector [512]   # the bits of the validators that signed the block (each bit represents a validator)
    syncCommitteeSignature: ByteVector [96]   # the signature of the sync committee
```

#### SyncCommittee

the public keys sync committee used within a period ( about 27h)


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L185).

```python
class SyncCommittee(Container):
    pubkeys        : Vector [blsPubky, 512]   # the 512 pubkeys (each 48 bytes) of the validators in the sync committee
    aggregatePubkey: ByteVector [48]          # the aggregate pubkey (48 bytes) of the sync committee
```

#### VoluntaryExit


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L123).

```python
class VoluntaryExit(Container):
    epoch         : Uint64
    validatorIndex: Uint64
```

#### Withdrawal


 The Type is defined in [chains/eth/ssz/beacon_denep.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/beacon_denep.c#L27).

```python
class Withdrawal(Container):
    index         : Uint64
    validatorIndex: Uint64
    address       : Address
    amount        : Uint64
```
### C4 ETH Request

The SSZ union type defintions defining datastructure of a proof for eth.

#### C4Request

the main container defining the incoming data processed by the verifier


 The Type is defined in [chains/eth/ssz/verify_types.c](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_types.c#L47).

```python
class C4Request(Container):
    version  : ByteVector [4]          # the [domain, major, minor, patch] version of the request, domaon=1 = eth
    data     : Union[                  # the data to proof
        None
        Bytes32                        # the blochash  which is used for blockhash proof
        Bytes[1073741824]              # the bytes of the data
        Uint256                        # the balance of an account
        EthTxData                      # the transaction data
        EthReceiptData                 # the transaction receipt
        List [EthReceiptDataLog, 1024] # result of eth_getLogs
        EthBlockData                   # the block data
        EthProofData                   # the result of an eth_getProof
    ]
    proof    : Union[                  # the proof of the data
        None
        EthAccountProof                # a Proof of an Account like eth_getBalance or eth_getStorageAt
        EthTransactionProof            # a Proof of a Transaction like eth_getTransactionByHash
        EthReceiptProof                # a Proof of a TransactionReceipt
        List [EthLogsBlock, 256]       # a Proof for multiple Receipts and txs
        EthCallProof                   # a Proof of a Call like eth_call
        EthSyncProof                   # Proof as input data for the sync committee transition used by zk
        EthBlockProof                  # Proof for BlockData
    ]
    sync_data: Union[                  # the sync data containing proofs for the transition between the two periods
        None
        List [LightClientUpdate, 512]  # this light client update can be fetched directly from the beacon chain API
    ]
```
### Proof Types

The SSZ type defintions used in the proofs.

#### EthAccountProof

1. **Patricia Merkle Proof** for the Account Object in the execution layer (balance, nonce, codeHash, storageHash) and the storage values with its own Proofs. (using eth_getProof): Result StateRoot
 2. **State Proof** is a SSZ Merkle Proof from the StateRoot to the ExecutionPayload over the BeaconBlockBody to its root hash which is part of the header.
 3. **BeaconBlockHeader** is passed because also need the slot in order to find out which period and which sync committee is used.
 4. **Signature of the SyncCommittee** (taken from the following block) is used to verify the SignData where the blockhash is part of the message and the Domain is calculated from the fork and the Genesis Validator Root.
 ```mermaid
 flowchart TB
     subgraph "ExecutionLayer"
         class ExecutionLayer transparent
         subgraph "Account"
             balance --> account
             nonce --> account
             codeHash --> account
             storageHash --> account
         end
         subgraph "Storage"
             key1 --..PM..-->storageHash
             key2 --..PM..-->storageHash
             key3 --..PM..-->storageHash
         end
     end
     subgraph "ConsensusLayer"
         subgraph "ExecutionPayload"
             account --..PM..--> stateRoot
         end
         subgraph "BeaconBlockBody"
             stateRoot --SSZ D:5--> executionPayload
             m[".."]
         end
         subgraph "BeaconBlockHeader"
             slot
             proposerIndex
             parentRoot
             s[stateRoot]
             executionPayload  --SSZ D:4--> bodyRoot
         end
     end
 ```


 The Type is defined in [chains/eth/ssz/verify_proof_types.h](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_proof_types.h#L180).

```python
class EthAccountProof(Container):
    accountProof: List [bytes_1024, 256]        # Patricia merkle proof
    address     : Address                       # the address of the account
    storageProof: List [EthStorageProof, 256]   # the storage proofs of the selected
    state_proof : EthStateProof                 # the state proof of the account
```

#### EthBlockProof

the stateRoot proof is used as part of different other types since it contains all relevant
 proofs to validate the stateRoot of the execution layer


 The Type is defined in [chains/eth/ssz/verify_proof_types.h](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_proof_types.h#L399).

```python
class EthBlockProof(Container):
    executionPayload        : Union[                # the merkle prooof from the executionPayload.state down to the blockBodyRoot hash
        DenepExecutionPayload
    ]
    proof                   : List [bytes32, 256]   # the merkle prooof from the executionPayload.state down to the blockBodyRoot hash
    header                  : BeaconBlockHeader     # the header of the beacon block
    sync_committee_bits     : BitVector [512]       # the bits of the validators that signed the block
    sync_committee_signature: ByteVector [96]       # the signature of the sync committee
```

#### EthCallAccount

1. **Patricia Merkle Proof** for the Account Object in the execution layer (balance, nonce, codeHash, storageHash) and the storage values with its own Proofs. (using eth_getProof): Result StateRoot
 2. **State Proof** is a SSZ Merkle Proof from the StateRoot to the ExecutionPayload over the BeaconBlockBody to its root hash which is part of the header.
 3. **BeaconBlockHeader** is passed because also need the slot in order to find out which period and which sync committee is used.
 4. **Signature of the SyncCommittee** (taken from the following block) is used to verify the SignData where the blockhash is part of the message and the Domain is calculated from the fork and the Genesis Validator Root.
 ```mermaid
 flowchart TB
     subgraph "ExecutionLayer"
         class ExecutionLayer transparent
         subgraph "Account"
             balance --> account
             nonce --> account
             codeHash --> account
             storageHash --> account
         end
         subgraph "Storage"
             key1 --..PM..-->storageHash
             key2 --..PM..-->storageHash
             key3 --..PM..-->storageHash
         end
     end
     subgraph "ConsensusLayer"
         subgraph "ExecutionPayload"
             account --..PM..--> stateRoot
         end
         subgraph "BeaconBlockBody"
             stateRoot --SSZ D:5--> executionPayload
             m[".."]
         end
         subgraph "BeaconBlockHeader"
             slot
             proposerIndex
             parentRoot
             s[stateRoot]
             executionPayload  --SSZ D:4--> bodyRoot
         end
     end
 ```


 The Type is defined in [chains/eth/ssz/verify_proof_types.h](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_proof_types.h#L237).

```python
class EthCallAccount(Container):
    accountProof: List [bytes_1024, 256]         # Patricia merkle proof
    address     : Address                        # the address of the account
    code        : Union[                         # the code of the contract
        Boolean                                  # no code delivered
        Bytes[4194304]                           # the code of the contract
    ]
    storageProof: List [EthStorageProof, 4096]   # the storage proofs of the selected
```

#### EthCallProof


 The Type is defined in [chains/eth/ssz/verify_proof_types.h](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_proof_types.h#L244).

```python
class EthCallProof(Container):
    accounts   : List [EthCallAccount, 256]   # used accounts
    state_proof: EthStateProof                # the state proof of the account
```

#### EthLogsBlock


 The Type is defined in [chains/eth/ssz/verify_proof_types.h](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_proof_types.h#L83).

```python
class EthLogsBlock(Container):
    blockNumber             : Uint64                  # the number of the execution block containing the transaction
    blockHash               : Bytes32                 # the blockHash of the execution block containing the transaction
    proof                   : List [bytes32, 64]      # the multi proof of the transaction, receipt_root,blockNumber and blockHash
    header                  : BeaconBlockHeader       # the header of the beacon block
    sync_committee_bits     : BitVector [512]         # the bits of the validators that signed the block
    sync_committee_signature: ByteVector [96]         # the signature of the sync committee
    txs                     : List [EthLogsTx, 256]   # the transactions of the block
```

#### EthLogsTx


 The Type is defined in [chains/eth/ssz/verify_proof_types.h](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_proof_types.h#L76).

```python
class EthLogsTx(Container):
    transaction     : Bytes[1073741824]       # the raw transaction payload
    transactionIndex: Uint32                  # the index of the transaction in the block
    proof           : List [bytes_1024, 64]   # the Merklr Patricia Proof of the transaction receipt ending in the receipt root
```

#### EthReceiptProof

represents the proof for a transaction receipt
 1. All Receipts of the execution blocks are serialized into a Patricia Merkle Trie and the merkle proof is created for the requested receipt.
 2. The **payload of the transaction** is used to create its SSZ Hash Tree Root from the BeaconBlock. This is needed in order to verify that the receipt actually belongs to the given transactionhash.
 3. The **SSZ Multi Merkle Proof** from the Transactions, Receipts, BlockNumber and BlockHash of the ExecutionPayload to the BlockBodyRoot. (Total Depth: 29)
 4. **BeaconBlockHeader** is passed because also need the slot in order to find out which period and which sync committee is used.
 5. **Signature of the SyncCommittee** (taken from the following block) is used to verify the SignData where the blockhash is part of the message and the Domain is calculated from the fork and the Genesis Validator Root.
 ```mermaid
 flowchart TB
     subgraph "ExecutionPayload"
         transactions
         receipts
         blockNumber
         blockHash
     end
     Receipt --PM--> receipts
     TX --SSZ D:21--> transactions
     subgraph "BeaconBlockBody"
         transactions  --SSZ D:5--> executionPayload
         blockNumber --SSZ D:5--> executionPayload
         blockHash --SSZ D:5--> executionPayload
         m[".."]
     end
     subgraph "BeaconBlockHeader"
         slot
         proposerIndex
         parentRoot
         s[stateRoot]
         executionPayload  --SSZ D:4--> bodyRoot
     end
 ```


 The Type is defined in [chains/eth/ssz/verify_proof_types.h](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_proof_types.h#L65).

```python
class EthReceiptProof(Container):
    transaction             : Bytes[1073741824]       # the raw transaction payload
    transactionIndex        : Uint32                  # the index of the transaction in the block
    blockNumber             : Uint64                  # the number of the execution block containing the transaction
    blockHash               : Bytes32                 # the blockHash of the execution block containing the transaction
    receipt_proof           : List [bytes_1024, 64]   # the Merklr Patricia Proof of the transaction receipt ending in the receipt root
    block_proof             : List [bytes32, 64]      # the multi proof of the transaction, receipt_root,blockNumber and blockHash
    header                  : BeaconBlockHeader       # the header of the beacon block
    sync_committee_bits     : BitVector [512]         # the bits of the validators that signed the block
    sync_committee_signature: ByteVector [96]         # the signature of the sync committee
```

#### EthStateBlockEnum

definition of an enum depending on the requested block


 The Type is defined in [chains/eth/ssz/verify_proof_types.h](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_proof_types.h#L8).

```python
class EthStateBlockEnum(Container):
               : None      # no block-proof for latest
    blockHash  : Bytes32   # proof for the right blockhash
    blockNumber: Uint64    # proof for the right blocknumber
```

#### EthStateProof

the stateRoot proof is used as part of different other types since it contains all relevant
 proofs to validate the stateRoot of the execution layer


 The Type is defined in [chains/eth/ssz/verify_proof_types.h](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_proof_types.h#L16).

```python
class EthStateProof(Container):
    block                   : Union[                # the block to be proven
                            : None                  # no block-proof for latest
        blockHash           : Bytes32               # proof for the right blockhash
        blockNumber         : Uint64                # proof for the right blocknumber
    proof                   : List [bytes32, 256]   # the merkle prooof from the executionPayload.state down to the blockBodyRoot hash
    header                  : BeaconBlockHeader     # the header of the beacon block
    sync_committee_bits     : BitVector [512]       # the bits of the validators that signed the block
    sync_committee_signature: ByteVector [96]       # the signature of the sync committee
```

#### EthStorageProof

represents the storage proof of a key. The value can be taken from the last entry, which is the leaf of the proof.


 The Type is defined in [chains/eth/ssz/verify_proof_types.h](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_proof_types.h#L26).

```python
class EthStorageProof(Container):
    key  : Bytes32                   # the key to be proven
    proof: List [bytes_1024, 1024]   # Patricia merkle proof
```

#### EthSyncProof

Proof as input data for the sync committee transition used by zk. This is a very compact proof mostly taken from the light client update.
 the proof itself is a merkle proof using the given gindex to verify from the hash of the pubkey all the way down to the signing root.
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


 The Type is defined in [chains/eth/ssz/verify_proof_types.h](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_proof_types.h#L381).

```python
class EthSyncProof(Container):
    oldKeys               : Vector [blsPubky, 512]   # the old keys which produced the signature
    newKeys               : Vector [blsPubky, 512]   # the new keys to be proven
    syncCommitteeBits     : BitVector [512]          # the bits of the validators that signed the block
    syncCommitteeSignature: ByteVector [96]          # the signature of the sync committee
    gidx                  : Uint64                   # the general index from the signing root to the pubkeys of the next_synccommittee
    proof                 : Vector [bytes32, 10]     # proof merkle proof from the signing root to the pubkeys of the next_synccommittee
    slot                  : Uint64                   # the slot of the block
    proposerIndex         : Uint64
```

#### EthTransactionProof

represents the account and storage values, including the Merkle proof, of the specified account.
 1. The **payload of the transaction** is used to create its SSZ Hash Tree Root.
 2. The **SSZ Merkle Proof** from the Transactions of the ExecutionPayload to the BlockBodyRoot. (Total Depth: 29)
 3. **BeaconBlockHeader** is passed because also need the slot in order to find out which period and which sync committee is used.
 4. **Signature of the SyncCommittee** (taken from the following block) is used to verify the SignData where the blockhash is part of the message and the Domain is calculated from the fork and the Genesis Validator Root.
 ```mermaid
 flowchart TB
     subgraph "ExecutionPayload"
         transactions
         blockNumber
         blockHash
     end
     TX --SSZ D:21--> transactions
     subgraph "BeaconBlockBody"
         transactions  --SSZ D:5--> executionPayload
         blockNumber --SSZ D:5--> executionPayload
         blockHash --SSZ D:5--> executionPayload
         m[".."]
     end
     subgraph "BeaconBlockHeader"
         slot
         proposerIndex
         parentRoot
         s[stateRoot]
         executionPayload  --SSZ D:4--> bodyRoot
     end
 ```


 The Type is defined in [chains/eth/ssz/verify_proof_types.h](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_proof_types.h#L123).

```python
class EthTransactionProof(Container):
    transaction             : Bytes[1073741824]    # the raw transaction payload
    transactionIndex        : Uint32               # the index of the transaction in the block
    blockNumber             : Uint64               # the number of the execution block containing the transaction
    blockHash               : Bytes32              # the blockHash of the execution block containing the transaction
    baseFeePerGas           : Uint64               # the baseFeePerGas
    proof                   : List [bytes32, 64]   # the multi proof of the transaction, blockNumber and blockHash
    header                  : BeaconBlockHeader    # the header of the beacon block
    sync_committee_bits     : BitVector [512]      # the bits of the validators that signed the block
    sync_committee_signature: ByteVector [96]      # the signature of the sync committee
```
### Data Types

The SSZ type defintions used in the data or the result of rpc-calls.

#### EthAccessListData

Entry in the access list of a transaction


 The Type is defined in [chains/eth/ssz/verify_data_types.h](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_data_types.h#L8).

```python
class EthAccessListData(Container):
    address    : Address
    storageKeys: List [bytes32, 256]
```

#### EthBlockData

display the block data , which is based on the execution payload


 The Type is defined in [chains/eth/ssz/verify_data_types.h](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_data_types.h#L76).

```python
class EthBlockData(Container):
    number               : Uint64                         # the blocknumber
    hash                 : Bytes32                        # the blockhash
    transactions         : Union[                         # the transactions
        as_hashes        : List [bytes32, 4096]           # the transactions hashes
        as_data          : List [EthTxData, 4096]         # the transactions data
    logsBloom            : ByteVector [256]               # the logsBloom
    receiptsRoot         : Bytes32                        # the receiptsRoot
    extraData            : Bytes[32]                      # the extraData
    withdrawalsRoot      : Bytes32                        # the withdrawalsRoot
    baseFeePerGas        : Uint256                        # the baseFeePerGas
    nonce                : ByteVector [8]                 # the nonce
    miner                : Address                        # the miner
    withdrawals          : List [DenepWithdrawal, 4096]   # the withdrawals
    excessBlobGas        : Uint64                         # the excessBlobGas
    difficulty           : Uint64                         # the difficulty
    gasLimit             : Uint64                         # the gasLimit
    gasUsed              : Uint64                         # the gasUsed
    timestamp            : Uint64                         # the timestamp
    mixHash              : Bytes32                        # the mixHash
    parentHash           : Bytes32                        # the parentHash
    uncles               : List [bytes32, 4096]           # the transactions hashes
    parentBeaconBlockRoot: Bytes32                        # the parentBeaconBlockRoot
    sha3Uncles           : Bytes32                        # the sha3Uncles of the uncles
    transactionsRoot     : Bytes32                        # the transactionsRoot
    stateRoot            : Bytes32                        # the stateRoot
    blobGasUsed          : Uint64                         # the gas used for the blob transactions
```

#### EthBlockDataTransactionUntion

the gasPrice of the transaction


 The Type is defined in [chains/eth/ssz/verify_data_types.h](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_data_types.h#L70).

```python
class EthBlockDataTransactionUntion(Container):
    as_hashes: List [bytes32, 4096]     # the transactions hashes
    as_data  : List [EthTxData, 4096]   # the transactions data
```

#### EthProofData


 The Type is defined in [chains/eth/ssz/verify_data_types.h](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_data_types.h#L112).

```python
class EthProofData(Container):
    balance     : Uint256
    codeHash    : Bytes32
    nonce       : Uint256
    storageHash : Bytes32
    accountProof: List [bytes_1024, 256]            # Patricia merkle proof
    storageProof: List [EthStorageProofData, 256]   # the storage proofs of the selected
```

#### EthReceiptData

the transaction data


 The Type is defined in [chains/eth/ssz/verify_data_types.h](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_data_types.h#L53).

```python
class EthReceiptData(Container):
    blockHash        : Bytes32                         # the blockHash of the execution block containing the transaction
    blockNumber      : Uint64                          # the number of the execution block containing the transaction
    transactionHash  : Bytes32                         # the hash of the transaction
    transactionIndex : Uint32                          # the index of the transaction in the block
    type             : Uint8                           # the type of the transaction
    from             : Address                         # the sender of the transaction
    to               : Bytes[20]                       # the target of the transaction
    cumulativeGasUsed: Uint64                          # the cumulative gas used
    gasUsed          : Uint64                          # the gas address of the created contract
    logs             : List [EthReceiptDataLog, 256]   # the logs of the transaction
    logsBloom        : ByteVector [256]                # the bloom filter of the logs
    status           : Uint8                           # the status of the transaction
    effectiveGasPrice: Uint64                          # the effective gas price of the transaction
```

#### EthReceiptDataLog

a log entry in the receipt


 The Type is defined in [chains/eth/ssz/verify_data_types.h](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_data_types.h#L39).

```python
class EthReceiptDataLog(Container):
    blockHash       : Bytes32             # the blockHash of the execution block containing the transaction
    blockNumber     : Uint64              # the number of the execution block containing the transaction
    transactionHash : Bytes32             # the hash of the transaction
    transactionIndex: Uint32              # the index of the transaction in the block
    address         : Address             # the address of the log
    logIndex        : Uint32              # the index of the log in the transaction
    removed         : Boolean             # whether the log was removed
    topics          : List [bytes32, 8]   # the topics of the log
    data            : Bytes[1073741824]   # the data of the log
```

#### EthStorageProofData

represents the storage proof of a key. The value can be taken from the last entry, which is the leaf of the proof.


 The Type is defined in [chains/eth/ssz/verify_data_types.h](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_data_types.h#L104).

```python
class EthStorageProofData(Container):
    key  : Bytes32                   # the key
    value: Bytes32                   # the value
    proof: List [bytes_1024, 1024]   # Patricia merkle proof
```

#### EthTxData

the transaction data


 The Type is defined in [chains/eth/ssz/verify_data_types.h](https://github.com/corpus-core/c4/blob/main/src/chains/eth/ssz/verify_data_types.h#L15).

```python
class EthTxData(Container):
    blockHash           : Bytes32                         # the blockHash of the execution block containing the transaction
    blockNumber         : Uint64                          # the number of the execution block containing the transaction
    hash                : Bytes32                         # the blockHash of the execution block containing the transaction
    transactionIndex    : Uint32                          # the index of the transaction in the block
    type                : Uint8                           # the type of the transaction
    nonce               : Uint64                          # the nonce of the transaction
    input               : Bytes[1073741824]               # the raw transaction payload
    r                   : Bytes32                         # the r value of the transaction
    s                   : Bytes32                         # the s value of the transaction
    chainId             : Uint32                          # the s value of the transaction
    v                   : Uint8                           # the v value of the transaction
    gas                 : Uint64                          # the gas limnit
    from                : Address                         # the sender of the transaction
    to                  : Bytes[20]                       # the target of the transaction
    value               : Uint256                         # the value of the transaction
    gasPrice            : Uint64                          # the gas price of the transaction
    maxFeePerGas        : Uint64                          # the maxFeePerGas of the transaction
    maxPriorityFeePerGas: Uint64                          # the maxPriorityFeePerGas of the transaction
    accessList          : List [EthAccessListData, 256]   # the access list of the transaction
    blobVersionedHashes : List [bytes32, 16]              # the blobVersionedHashes of the transaction
    yParity             : Uint8                           # the yParity of the transaction
```
## License

MIT

