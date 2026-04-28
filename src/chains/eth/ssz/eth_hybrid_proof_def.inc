/* Included from `verify_types.c` after `verify_proof_core.h` so hybrid layouts are complete for `sizeof`. */

const ssz_def_t ETH_HYBRID_ACCOUNT_PROOF[] = {
    SSZ_LIST("accountProof", ssz_bytes_1024, 256),
    SSZ_ADDRESS("address"),
    SSZ_LIST("storageProof", ETH_STORAGE_PROOF_CONTAINER, 256),
    SSZ_CONTAINER("header_data", ETH_BLOCK_HEADER_DATA),
};

const ssz_def_t ETH_HYBRID_CALL_PROOF[] = {
    SSZ_LIST("accounts", ETH_CALL_ACCOUNT_CONTAINER, 256),
    SSZ_CONTAINER("header_data", ETH_BLOCK_HEADER_DATA),
};

const ssz_def_t ETH_HYBRID_TRANSACTION_PROOF[] = {
    SSZ_BYTES("transaction", 1073741824),
    SSZ_UINT32("transactionIndex"),
    SSZ_LIST("txProof", ssz_bytes32, 64),
    SSZ_CONTAINER("header_data", ETH_BLOCK_HEADER_DATA),
};

const ssz_def_t ETH_HYBRID_RECEIPT_PROOF[] = {
    SSZ_BYTES("transaction", 1073741824),
    SSZ_UINT32("transactionIndex"),
    SSZ_LIST("txProof", ssz_bytes32, 64),
    SSZ_LIST("receipt_proof", ssz_bytes_1024, 64),
    SSZ_CONTAINER("header_data", ETH_BLOCK_HEADER_DATA),
};

const ssz_def_t ETH_HYBRID_LOGS_BLOCK[] = {
    SSZ_CONTAINER("header_data", ETH_BLOCK_HEADER_DATA),
    SSZ_LIST("txProof", ssz_bytes32, 256),
    SSZ_LIST("txs", ETH_LOGS_TX_CONTAINER, 256),
};

const ssz_def_t ETH_HYBRID_LOGS_BLOCK_CONTAINER = SSZ_CONTAINER("HybridLogsBlock", ETH_HYBRID_LOGS_BLOCK);

const ssz_def_t ETH_HYBRID_BLOCK_RECEIPTS_PROOF[] = {
    SSZ_LIST("transactions", ssz_transactions_bytes, 1048576),
    SSZ_LIST("receipts", ssz_bytes_list, 65536),
    SSZ_CONTAINER("header_data", ETH_BLOCK_HEADER_DATA),
};

const ssz_def_t ETH_HYBRID_BLOCK_PROOF[] = {
    SSZ_UNION("executionPayload", ETH_EXECUTION_PAYLOAD_UNION),
};

const ssz_def_t ETH_HYBRID_BLOCK_HEADER_PROOF[] = {
    SSZ_CONTAINER("header_data", ETH_BLOCK_HEADER_DATA),
};
