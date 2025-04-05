// title: C4 ETH Request
// description: The SSZ union type defintions defining datastructure of a proof for eth.

#include "beacon_types.h"
#include "ssz.h"

// A List of possible types of data matching the Proofs
static const ssz_def_t C4_REQUEST_DATA_UNION[] = {
    SSZ_NONE,
    SSZ_BYTES32("hash"),                                       // the blochash  which is used for blockhash proof
    SSZ_BYTES("bytes", 1073741824),                            // the bytes of the data
    SSZ_UINT256("value"),                                      // the balance of an account
    SSZ_CONTAINER("EthTransactionData", ETH_TX_DATA),          // the transaction data
    SSZ_CONTAINER("EthReceiptData", ETH_RECEIPT_DATA),         // the transaction receipt
    SSZ_LIST("EthLogs", ETH_RECEIPT_DATA_LOG_CONTAINER, 1024), // result of eth_getLogs
    SSZ_CONTAINER("EthBlockData", ETH_BLOCK_DATA)};            // the block data

// A List of possible types of proofs matching the Data
static const ssz_def_t C4_REQUEST_PROOFS_UNION[] = {
    SSZ_NONE,
    SSZ_CONTAINER("AccountProof", ETH_ACCOUNT_PROOF),         // a Proof of an Account like eth_getBalance or eth_getStorageAt
    SSZ_CONTAINER("TransactionProof", ETH_TRANSACTION_PROOF), // a Proof of a Transaction like eth_getTransactionByHash
    SSZ_CONTAINER("ReceiptProof", ETH_RECEIPT_PROOF),         // a Proof of a TransactionReceipt
    SSZ_LIST("LogsProof", ETH_LOGS_BLOCK_CONTAINER, 256),     // a Proof for multiple Receipts and txs
    SSZ_CONTAINER("CallProof", ETH_CALL_PROOF),               // a Proof of a Call like eth_call
    SSZ_CONTAINER("SyncProof", ETH_SYNC_PROOF),               // Proof as input data for the sync committee transition used by zk
    SSZ_CONTAINER("BlockProof", ETH_BLOCK_PROOF),             // Proof for BlockData
}; // a Proof for multiple accounts

// A List of possible types of sync data used to update the sync state by verifying the transition from the last period to the required.
static const ssz_def_t C4_REQUEST_SYNCDATA_UNION[] = {
    SSZ_NONE,
    SSZ_LIST("LightClientUpdate", LIGHT_CLIENT_UPDATE_CONTAINER, 512)}; // this light client update can be fetched directly from the beacon chain API

// the main container defining the incoming data processed by the verifier
static const ssz_def_t C4_REQUEST[] = {
    SSZ_BYTE_VECTOR("version", 4),                      // the [domain, major, minor, patch] version of the request, domaon=1 = eth
    SSZ_UNION("data", C4_REQUEST_DATA_UNION),           // the data to proof
    SSZ_UNION("proof", C4_REQUEST_PROOFS_UNION),        // the proof of the data
    SSZ_UNION("sync_data", C4_REQUEST_SYNCDATA_UNION)}; // the sync data containing proofs for the transition between the two periods

static const ssz_def_t C4_REQUEST_CONTAINER = SSZ_CONTAINER("C4Request", C4_REQUEST);
