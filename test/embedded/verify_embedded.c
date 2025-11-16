/*
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "util/bytes.h"
#include "util/json.h"
#include "util/plugin.h"
#include "verifier/verify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Debug function to print to both stdout and stderr
static void debug_print(const char* message) {
  buffer_t buf = {0};
  bprintf(&buf, "%s\n", message);
  fwrite(buf.data.data, 1, buf.data.len, stdout);
  fwrite(buf.data.data, 1, buf.data.len, stderr);
  buffer_free(&buf);
}

// SD Card paths - QEMU mounts the SD card at /sd in the filesystem
#define SD_PATH "/sd/"

// Static buffer for proof data

// Embedded storage plugin implementation
static bool embedded_storage_get(char* filename, buffer_t* data) {
  debug_print("----------------------------------");
  debug_print("Attempting to read file...");

  buffer_t msg = {0};
  bprintf(&msg, "Filename: %s\n", filename);
  fwrite(msg.data.data, 1, msg.data.len, stdout);
  buffer_free(&msg);

  // Try to open the file in the current directory
  FILE* f = fopen(filename, "rb");
  if (!f) {
    debug_print("Failed to open file");

    // Try alternate locations - for debugging only
    char     alt_path[256];
    buffer_t tmp_buf = stack_buffer(alt_path);
    bprintf(&tmp_buf, "/tmp/%s", filename);
    debug_print("Trying alternate path...");

    msg = (buffer_t) {0};
    bprintf(&msg, "Alt path: %s\n", alt_path);
    fwrite(msg.data.data, 1, msg.data.len, stdout);
    buffer_free(&msg);

    f = fopen(alt_path, "rb");
    if (!f) {
      debug_print("Still failed to open file from alternate path");
      return false;
    }
  }
  debug_print("File opened successfully");

  // Get file size
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  msg = (buffer_t) {0};
  bprintf(&msg, "File size: %d bytes\n", (int32_t) fsize);
  fwrite(msg.data.data, 1, msg.data.len, stdout);
  buffer_free(&msg);

  if (fsize <= 0) {
    debug_print("Invalid file size");
    fclose(f);
    return false;
  }

  // Check if file fits in buffer
  if (data->allocated < 0 && fsize > -data->allocated) {
    debug_print("Buffer too small for file");
    fclose(f);
    return false;
  }

  buffer_grow(data, fsize); // this will make sure the buffer is big enough

  // Ensure buffer was allocated properly
  if (!data->data.data) {
    debug_print("Failed to allocate buffer");
    fclose(f);
    return false;
  }

  size_t read = fread(data->data.data, 1, fsize, f);
  fclose(f);
  if (read != fsize) {
    msg = (buffer_t) {0};
    bprintf(&msg, "Read %d bytes, expected %d\n", (uint32_t) read, (int32_t) fsize);
    fwrite(msg.data.data, 1, msg.data.len, stdout);
    buffer_free(&msg);
    debug_print("Failed to read entire file");
    return false;
  }

  msg = (buffer_t) {0};
  bprintf(&msg, "Successfully read %d bytes\n", (uint32_t) read);
  fwrite(msg.data.data, 1, msg.data.len, stdout);
  buffer_free(&msg);
  data->data.len = fsize;
  return true;
}

static void embedded_storage_set(char* key, bytes_t value) {
  // In a real system, implement storage to flash/EEPROM
  buffer_t msg = {0};
  bprintf(&msg, "Storage set called for key: %s (not implemented)\n", key);
  fwrite(msg.data.data, 1, msg.data.len, stdout);
  buffer_free(&msg);
  (void) key;
  (void) value;
}

static void embedded_storage_delete(char* key) {
  // In a real system, implement deletion from flash/EEPROM
  buffer_t msg = {0};
  bprintf(&msg, "Storage delete called for key: %s (not implemented)\n", key);
  fwrite(msg.data.data, 1, msg.data.len, stdout);
  buffer_free(&msg);
  (void) key;
}

// Return codes
#define TEST_SUCCESS       0
#define TEST_FAILED_READ   1
#define TEST_FAILED_VERIFY 2

// Embedded test entry point
int main(void) {
  debug_print("Starting embedded verification test");

  // Static allocation for fixed-size data
  static verify_ctx_t verify_ctx = {0};

  // Create proof path
  debug_print("Setting up test...");

  // Check current directory
  debug_print("Listing files in current directory:");
  FILE* f = fopen(".", "r");
  if (f) {
    fclose(f);
    debug_print("Current directory exists");
  }
  else {
    debug_print("Current directory not accessible");
  }

  // Check if proof file exists
  f = fopen("proof.ssz", "rb");
  if (f) {
    fclose(f);
    debug_print("Proof file exists");
  }
  else {
    debug_print("Proof file not found");
    // Create a zero-length test file for diagnostic
    f = fopen("test_write.txt", "w");
    if (f) {
      buffer_t test_msg = {0};
      bprintf(&test_msg, "Test write\n");
      fwrite(test_msg.data.data, 1, test_msg.data.len, f);
      buffer_free(&test_msg);
      fclose(f);
      debug_print("Created test file");
    }
    else {
      debug_print("Failed to create test file - directory may be read-only");
    }
  }

  // Configure storage plugin
  debug_print("Configuring storage plugin...");
  storage_plugin_t storage = {
      .get             = embedded_storage_get,
      .set             = embedded_storage_set,
      .del             = embedded_storage_delete,
      .max_sync_states = 1 // We only need one state for this test
  };
  c4_set_storage_config(&storage);

  // Use the storage plugin to load the proof into heap
  debug_print("Loading proof file...");
  buffer_t proof_buf = {0};
  if (!storage.get("proof.ssz", &proof_buf)) {
    debug_print("Failed to read proof file");
    return TEST_FAILED_READ;
  }

  // Create JSON parameters
  debug_print("Creating JSON parameters...");
  json_t params = json_parse("[{\"address\":[\"0xdac17f958d2ee523a2206206994597c13d831ec7\"],\"fromBlock\":\"0x14d7970\",\"toBlock\":\"0x14d7970\"}]");

  // Check if JSON params were parsed
  if (params.type == JSON_TYPE_INVALID) {
    debug_print("Failed to parse JSON parameters");
    if (proof_buf.data.data) safe_free(proof_buf.data.data);
    return TEST_FAILED_VERIFY;
  }

  // Verify the proof
  debug_print("Verifying proof...");
  c4_verify_from_bytes(&verify_ctx, proof_buf.data, "eth_getLogs", params, C4_CHAIN_MAINNET);

  // Cleanup
  debug_print("Cleaning up...");
  if (proof_buf.data.data) safe_free(proof_buf.data.data);

  // Check verification result
  if (!verify_ctx.success || verify_ctx.state.error) {
    buffer_t err_msg = {0};
    bprintf(&err_msg, "Verification failed: %s\n", verify_ctx.state.error ? verify_ctx.state.error : "unknown error");
    fwrite(err_msg.data.data, 1, err_msg.data.len, stdout);
    buffer_free(&err_msg);
    debug_print("Verification failed");
    return TEST_FAILED_VERIFY;
  }

  debug_print("Verification successful");
  debug_print("Test completed successfully!");
  return TEST_SUCCESS;
}