#include "witness.h"
#include "crypto.h"

const ssz_def_t C4_BLOCK_HASH_WITNESS[3] = {
    SSZ_UINT64("chainId"),          // the chainId
    SSZ_UINT64("blockNumber"),      // blocknumber
    SSZ_BYTES32("blockHash"),       // the blockhash
    SSZ_BYTES32("stateRoot"),       // the state root
    SSZ_BYTES32("receiptsRoot"),    // the receipts root
    SSZ_BYTES32("transactionsRoot") // the transactions root
}; // the blockhash seen

static const ssz_def_t C4_WITNESS_UNION[] = {
    SSZ_CONTAINER(C4_BLOCK_HASH_WITNESS_ID, C4_BLOCK_HASH_WITNESS), // the blockhash
};

const ssz_def_t C4_WITNESS_CONTAINER[2] = {
    SSZ_UNION("data", C4_WITNESS_UNION), // the data seen
    SSZ_BYTE_VECTOR("signature", 65),    // the signature of the witness
};

ssz_builder_t c4_witness_sign(ssz_builder_t data, bytes32_t private_key) {
  buffer_t  buffer = {0};
  bytes32_t hash;
  uint8_t   signature[65];
  buffer_append(&buffer, data.fixed.data);
  buffer_append(&buffer, data.dynamic.data);
  ssz_ob_t data_ob = {.def = data.def, .bytes = buffer.data};
  ssz_hash_tree_root(data_ob, hash);

  // sign
  secp256k1_sign(private_key, hash, signature);
  buffer_free(&buffer);

  ssz_builder_t builder = ssz_builder_for_def(C4_WITNESS_CONTAINER);
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