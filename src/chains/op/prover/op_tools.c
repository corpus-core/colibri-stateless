#include "beacon.h"
#include "beacon_types.h"
#include "bytes.h"
#include "op_payload.h"
#include "op_prover.h"
#include "op_types.h"
#include "ssz.h"
#include "version.h"

static void set_data(ssz_builder_t* req, const char* name, ssz_builder_t data) {
  if (data.fixed.data.data || data.dynamic.data.data)
    ssz_add_builders(req, name, data);
  else
    ssz_add_bytes(req, name, bytes(NULL, 1));
}

bytes_t op_create_proof_request(chain_id_t chain_id, ssz_builder_t data, ssz_builder_t proof, ssz_builder_t sync_data) {
  ssz_builder_t c4_req    = (ssz_builder_t) {.def = op_ssz_verification_type(OP_SSZ_VERIFY_REQUEST), .dynamic = {0}, .fixed = {0}};
  uint8_t       vbytes[4] = {0};
  memcpy(vbytes, c4_protocol_version_bytes, 4);
  vbytes[0] = C4_CHAIN_TYPE_OP;

  // build the request
  ssz_add_bytes(&c4_req, "version", bytes(vbytes, 4));
  set_data(&c4_req, "data", data);
  set_data(&c4_req, "proof", proof);
  set_data(&c4_req, "sync_data", sync_data);

  // set chain_engine
  *c4_req.fixed.data.data = (uint8_t) c4_chain_type(chain_id);
  return ssz_builder_to_bytes(&c4_req).bytes;
}

ssz_ob_t* op_get_execution_payload(ssz_builder_t* block_proof) {
  return op_decode_preconf_builder_execution_payload(block_proof);
}
