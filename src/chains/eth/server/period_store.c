
#include "period_store.h"
#include "eth_conf.h"
#include "period_store_zk_prover.h"

bool c4_ps_file_exists(uint64_t period, const char* filename) {
  char*       path = bprintf(NULL, "%s/%l/%s", eth_config.period_store, period, filename);
  struct stat buffer;
  bool        exists = stat(path, &buffer) == 0;
  safe_free(path);
  return exists;
}

void c4_period_sync_on_head(uint64_t slot, const uint8_t block_root[32], const uint8_t header112[112]) {
  if (!eth_config.period_store) return;

  block_t block = {.slot = slot};
  memcpy(block.root, block_root, 32);
  memcpy(block.header, header112, 112);
  memcpy(block.parent_root, header112 + 16, 32);

  c4_ps_set_block(&block, false);
}

void c4_period_sync_on_checkpoint(bytes32_t checkpoint, uint64_t slot) {
  uint64_t period = slot >> 13;
  if (!eth_config.period_store) return;

  if (!eth_config.period_master_url) {
    if (!c4_ps_file_exists(period, "lcb.ssz")) c4_ps_fetch_lcb_for_checkpoint(checkpoint, period);
    if (!c4_ps_file_exists(period, "lcu.ssz")) c4_ps_schedule_fetch_lcu(period);
    if (!c4_ps_file_exists(period, "historical_root.json")) c4_ps_schedule_fetch_historical_root(period);
    if (c4_ps_file_exists(period, "zk_proof_g16.bin")) c4_build_zk_sync_proof_data(period);
    if (c4_ps_file_exists(period + 1, "zk_proof_g16.bin")) c4_build_zk_sync_proof_data(period + 1);
    c4_period_prover_on_checkpoint(period);
  }
  else
    // Slave instance: optionally full-sync the period store from master for backup purposes.
    c4_ps_full_sync_on_checkpoint(period);
}
