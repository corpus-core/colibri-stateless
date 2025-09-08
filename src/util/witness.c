#include "witness.h"
#include "crypto.h"

// : Ethereum

// :: Witness Proof

// When validating Layer 2 transactions and calls, a full proof can only be created once the L2 block is  commited to l1.
// For most l2 this may take a long time, which would mean there is no way to verify a transaction for up to 30mins.
// In order to make the verification available for all L2, we can use a witness proof.
//
// a Witness Proof is a signed data like a blockhash as seen by a witness.
// The colibri client can be configured to accept such witness proofs by defining one or more urls and public addresses,
// testifying that even if the L2 block is not yet committed to l1, the witness has seen the blockhash and can attest to it.
// This allows us to verify the transaction or call even if the L2 block is not yet committed to l1.

// ## BlockHash Witness
//
// The BlockHash Witness is a witness proof that contains the blockhash along with the most critical data of an block. This can then be used to verify other data.
const ssz_def_t C4_BLOCK_HASH_WITNESS[6] = {
    SSZ_UINT64("chainId"),          // the chainId
    SSZ_UINT64("blockNumber"),      // blocknumber
    SSZ_BYTES32("blockHash"),       // the blockhash
    SSZ_BYTES32("stateRoot"),       // the state root
    SSZ_BYTES32("receiptsRoot"),    // the receipts root
    SSZ_BYTES32("transactionsRoot") // the transactions root
};

static const ssz_def_t C4_WITNESS_UNION[] = {
    SSZ_CONTAINER(C4_BLOCK_HASH_WITNESS_ID, C4_BLOCK_HASH_WITNESS), // the blockhash
};

// ## The Signging Envelope
//
// The data signed is the always the hash_tree_root of the data to verify.
// the signature itself is a ecdsa secp256k1 signature where the last byte is the recovery byte
const ssz_def_t C4_WITNESS_SIGNED[2] = {
    SSZ_UNION("data", C4_WITNESS_UNION), // the data seen
    SSZ_BYTE_VECTOR("signature", 65),    // the signature of the witness
};

const ssz_def_t C4_WITNESS_SIGNED_CONTAINER = SSZ_CONTAINER("WitnessProof", C4_WITNESS_SIGNED);

ssz_def_t* c4_witness_get_def(const char* name) {
  for (int i = 0; i < sizeof(C4_WITNESS_UNION) / sizeof(C4_WITNESS_UNION[0]); i++) {
    if (strcmp(name, C4_WITNESS_UNION[i].name) == 0) return C4_WITNESS_UNION + i;
  }
  return NULL;
}

ssz_builder_t c4_witness_sign(ssz_builder_t data, bytes32_t private_key) {
  buffer_t  buffer = {0};
  bytes32_t hash;
  uint8_t   signature[65];
  buffer_append(&buffer, data.fixed.data);
  buffer_append(&buffer, data.dynamic.data);
  ssz_ob_t data_ob = {.def = data.def, .bytes = buffer.data};
  ssz_hash_tree_root(data_ob, hash);

// sign
#ifdef WITNESS_SIGNER
  secp256k1_sign(private_key, hash, signature);
#else
  memset(signature, 0, 65);
#endif
  buffer_free(&buffer);

  ssz_builder_t builder = ssz_builder_for_def(&C4_WITNESS_SIGNED_CONTAINER);
  ssz_add_builders(&builder, "data", data);
  ssz_add_bytes(&builder, "signature", bytes(signature, 65));
  return builder;
}

bool c4_witness_verify(ssz_ob_t witness, ssz_ob_t* data, address_t address) {
  ssz_ob_t  data_ob      = ssz_get(&witness, "data");
  ssz_ob_t  signature_ob = ssz_get(&witness, "signature");
  bytes32_t hash;
  uint8_t   pub[64];
  ssz_hash_tree_root(data_ob, hash);
  if (!secp256k1_recover(hash, signature_ob.bytes, pub)) return false;
  keccak(bytes(pub, 64), hash);
  memcpy(address, hash + 12, 20);
  return true;
}