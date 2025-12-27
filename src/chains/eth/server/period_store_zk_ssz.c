/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */
#include "../zk_verifier/zk_verifier.h"
#include "../zk_verifier/zk_verifier_constants.h"
#include "beacon_types.h"
#include "bytes.h"
#include "eth_conf.h"
#include "logger.h"
#include "period_store.h"
#include "period_store_zk_prover.h"
#include "server.h"
#include "uv_util.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <uv.h>

#define HEADER_SIZE 112

static bytes_t get_header(bytes_t headers, uint64_t slot, uint64_t slot_offset) {
  uint64_t offset = (slot - slot_offset) * HEADER_SIZE;
  return headers.len < offset + HEADER_SIZE ? NULL_BYTES : bytes_slice(headers, offset, HEADER_SIZE);
}

static void files_write_cb(void* user_data, file_data_t* files, int num_files) {
  if (files[0].error)
    log_error("Prover: Failed to write zk_proof.ssz for period %l: %s", user_data, files[0].error);
  else
    log_info("Prover: Wrote zk_proof.ssz for period %l", user_data);
  c4_file_data_array_free(files, num_files, 1);
}
static void files_read_cb(void* user_data, file_data_t* files, int num_files) {
  uint64_t period = (uint64_t) user_data;
  if (files[0].error) {
    log_error("Prover: Failed to read zk_proof_g16.bin while building zk_sync_proof_data for period %l: %s", period, files[0].error);
    c4_file_data_array_free(files, num_files, 1);
    return;
  }
  if (files[1].error) {
    log_error("Prover: Failed to read sync.ssz while building zk_sync_proof_data for period %l: %s", period, files[1].error);
    c4_file_data_array_free(files, num_files, 1);
    return;
  }
  if (files[2].error) {
    log_error("Prover: Failed to read headers.ssz while building zk_sync_proof_data for period %l: %s", period, files[2].error);
    c4_file_data_array_free(files, num_files, 1);
    return;
  }

  ssz_ob_t sync                       = (ssz_ob_t) {.def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST), .bytes = files[1].data};
  sync                                = ssz_get(&sync, "proof");
  const chain_spec_t* spec            = c4_eth_get_chain_spec(http_server.chain_id);
  uint64_t            slots_per_epoch = 1 << spec->slots_per_epoch_bits;
  uint64_t            slot_offset     = (period - 1) << 13;
  bytes_t             headers         = files[2].data;
  uint64_t            slot            = ssz_get_uint64(&sync, "slot");
  uint64_t            checkpoint      = slots_per_epoch + (slot - (slot % slots_per_epoch));
  bytes_t             header          = get_header(headers, slot, slot_offset);

  while (get_header(headers, checkpoint, slot_offset).len == HEADER_SIZE && bytes_all_zero(get_header(headers, checkpoint, slot_offset))) checkpoint += slots_per_epoch;
  bytes_t checkpoint_header = get_header(headers, checkpoint, slot_offset);
  if (checkpoint_header.len == 0) {
    log_error("Prover: Checkpoint header for zk proof of period %l is not found", period);
    c4_file_data_array_free(files, num_files, 1);
    return;
  }
  if (header.len == 0) {
    log_error("Prover: Attested header for zk proof of period %l is not found", period);
    c4_file_data_array_free(files, num_files, 1);
    return;
  }

  // find checkpoint
  buffer_t headers_list = {0};
  for (uint64_t i = slot + 1; i < checkpoint; i++) {
    bytes_t h = get_header(headers, i, slot_offset);
    if (h.len == 0 || bytes_all_zero(h)) continue;
    buffer_append(&headers_list, bytes_slice(h, 0, 16));  // slot and proposerIndex
    buffer_append(&headers_list, bytes_slice(h, 48, 64)); // stateRoot and bodyRoot
  }

  ssz_builder_t builder = ssz_builder_for_def(C4_ETH_REQUEST_SYNCDATA_UNION + 2);
  // build checkpoint proof for ETH_HEADERS_BLOCK_PROOF
  ssz_builder_t checkpoint_builder = ssz_builder_for_def(ssz_get_def(builder.def, "checkpoint")->def.container.elements + 2);
  ssz_add_bytes(&checkpoint_builder, "headers", headers_list.data);
  ssz_add_bytes(&checkpoint_builder, "header", checkpoint_header);
  ssz_add_bytes(&checkpoint_builder, "sync_committee_bits", bytes(NULL, 64));
  ssz_add_bytes(&checkpoint_builder, "sync_committee_signature", bytes(NULL, 96));

  ssz_add_bytes(&builder, "vk_hash", bytes(VK_PROGRAM_HASH, 32));
  ssz_add_bytes(&builder, "proof", files[0].data);
  ssz_add_bytes(&builder, "header", header);
  ssz_add_bytes(&builder, "pubkeys", ssz_get(&sync, "newKeys").bytes);
  ssz_add_builders(&builder, "checkpoint", checkpoint_builder);
  ssz_add_bytes(&builder, "signatures", NULL_BYTES);

  c4_file_data_array_free(files, num_files, 1);
  file_data_t file = {
      .data = ssz_builder_to_bytes(&builder).bytes,
      .path = bprintf(NULL, "%s/%l/zk_proof.ssz", eth_config.period_store, period)};

  c4_write_files_uv((void*) period, files_write_cb, &file, 1, O_RDWR | O_CREAT, 0666);
}

void c4_build_zk_sync_proof_data(uint64_t period) {
  if (c4_ps_file_exists(period, "zk_proof.ssz")) return;
  file_data_t files[3] = {0};
  files[0].path        = bprintf(NULL, "%s/%l/zk_proof_g16.bin", eth_config.period_store, period);
  files[1].path        = bprintf(NULL, "%s/%l/sync.ssz", eth_config.period_store, period);
  files[2].path        = bprintf(NULL, "%s/%l/headers.ssz", eth_config.period_store, period - 1);

  c4_read_files_uv((void*) period, files_read_cb, files, 3);
}
