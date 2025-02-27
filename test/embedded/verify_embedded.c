#include "util/bytes.h"
#include "util/json.h"
#include "util/plugin.h"
#include "verifier/verify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Static buffer for proof data

// Embedded storage plugin implementation
static bool embedded_storage_get(char* filename, buffer_t* data) {
  // In a real embedded system, this would read from flash/EEPROM/etc
  FILE* f = fopen(filename, "rb");
  if (!f) return false;

  // Get file size
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  // Check if file fits in buffer
  if (data->allocated < 0 && fsize > -data->allocated) {
    fclose(f);
    return false;
  }

  buffer_grow(data, fsize); // this will make sure the buffer is big enough
  size_t read = fread(data->data.data, 1, fsize, f);
  fclose(f);
  if (read != fsize) return false;
  data->data.len = fsize;
  return true;
}

static void embedded_storage_set(char* key, bytes_t value) {
  // In a real system, implement storage to flash/EEPROM
  (void) key;
  (void) value;
}

static void embedded_storage_delete(char* key) {
  // In a real system, implement deletion from flash/EEPROM
  (void) key;
}

// Return codes
#define TEST_SUCCESS       0
#define TEST_FAILED_READ   1
#define TEST_FAILED_VERIFY 2

// Embedded test entry point
int main(void) {
  // Static allocation for fixed-size data
  static verify_ctx_t verify_ctx = {0};

  // Configure storage plugin
  storage_plugin_t storage = {
      .get             = embedded_storage_get,
      .set             = embedded_storage_set,
      .del             = embedded_storage_delete,
      .max_sync_states = 1 // We only need one state for this test
  };
  c4_set_storage_config(&storage);

  // Use the storage plugin to load the proof into heap
  buffer_t proof_buf = {0};
  if (!storage.get("proof.ssz", &proof_buf)) {
    printf("Failed to read proof file\n");
    return TEST_FAILED_READ;
  }

  // Create JSON parameters
  json_t params = json_parse("[{\"address\":[\"0xdac17f958d2ee523a2206206994597c13d831ec7\"],\"fromBlock\":\"0x14d7970\",\"toBlock\":\"0x14d7970\"}]");

  // Verify the proof
  c4_verify_from_bytes(&verify_ctx, proof_buf.data, "eth_getLogs", params, C4_CHAIN_MAINNET);

  // Cleanup
  if (proof_buf.data.data) free(proof_buf.data.data);

  // Check verification result
  if (!verify_ctx.success || verify_ctx.state.error) {
    printf("Verification failed: %s\n", verify_ctx.state.error ? verify_ctx.state.error : "unknown error");
    return TEST_FAILED_VERIFY;
  }

  printf("Verification successful\n");
  return TEST_SUCCESS;
}