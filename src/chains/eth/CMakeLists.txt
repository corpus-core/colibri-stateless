add_verifier(
  NAME eth_verifier
  SOURCES 
    verifier/sync_committee.c
    verifier/sync_committee_state.c
    verifier/verify_blockhash_proof.c
    verifier/verify_account_proof.c
    verifier/verify_tx_proof.c
    verifier/verify_receipt_proof.c
    verifier/verify_logs_proof.c
    verifier/eth_tx.c
    verifier/eth_verify.c
    verifier/patricia_trie.c
    verifier/patricia.c
    verifier/rlp.c
    ssz/verify_types.c
    ssz/beacon_types.c
    ssz/beacon_denep.c
    ssz/beacon_electra.c
  DEPENDS util
  GET_REQ_TYPE c4_eth_get_request_type
  VERIFY c4_eth_verify
)

add_proofer(
  NAME eth_proofer
  SOURCES 
    proofer/eth_proofer.c
    proofer/proof_account.c
    proofer/proof_transaction.c
    proofer/proof_logs.c
    proofer/beacon.c
    proofer/proof_receipt.c
    proofer/eth_req.c
    proofer/beacon_cache.c
    proofer/eth_tools.c
  DEPS util eth_verifier
  PROOF eth_proofer_execute
)

