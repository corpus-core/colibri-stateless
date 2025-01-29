#include "../../src/proofer/proofer.h"
#include "../../src/proofer/ssz_types.h"
#include "../../src/util/bytes.h"
#include "../../src/util/crypto.h"
#include "../../src/util/json.h"
#include "../../src/util/plugin.h"
#include "../../src/util/request.h"
#include "../../src/util/ssz.h"
#include "../../src/verifier/sync_committee.h"
#include "../../src/verifier/types_verify.h"
#include "../../src/verifier/verify.h"
#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_HEX_STRING_EQUAL(expected_hex, actual_array, size, message)              \
  do {                                                                                  \
    uint8_t expected_bytes[size];                                                       \
    hex_to_bytes(expected_hex, -1, bytes(expected_bytes, size));                        \
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected_bytes, actual_array, size, message); \
  } while (0)

typedef struct _cached {
  bytes_t         data;
  char*           filename;
  struct _cached* next;
} _cached_t;
static _cached_t* _file_cache = NULL;
static bool       file_get(char* filename, buffer_t* data) {
  for (_cached_t* val = _file_cache; val; val = val->next) {
    if (strcmp(val->filename, filename) == 0) {
      buffer_append(data, val->data);
      return true;
    }
  }
  return false;
}
static void file_delete(char* filename) {
  _cached_t** prev = &_file_cache;
  for (_cached_t* val = _file_cache; val; prev = &val->next, val = val->next) {
    if (strcmp(val->filename, filename) == 0) {
      *prev = val->next;
      free(val->filename);
      free(val->data.data);
      free(val);
      return;
    }
  }
}

static void file_set(char* key, bytes_t value) {
  _cached_t* n = calloc(1, sizeof(_cached_t));
  n->filename  = strdup(key);
  n->data      = bytes_dup(value);
  n->next      = _file_cache;
  _file_cache  = n;
}

static void reset_local_filecache() {
  while (_file_cache) {
    _cached_t* next = _file_cache->next;
    free(_file_cache->filename);
    free(_file_cache->data.data);
    free(_file_cache);
    _file_cache = next;
  }

  storage_plugin_t plgn = {
      .del             = file_delete,
      .get             = file_get,
      .set             = file_set,
      .max_sync_states = 1};
  c4_set_storage_config(&plgn);
}

static bytes_t read_testdata(const char* filename) {
  unsigned char buffer[1024];
  size_t        bytesRead;
  buffer_t      data = {0};
  buffer_t      path = {0};
  buffer_add_chars(&path, TESTDATA_DIR "/");
  buffer_add_chars(&path, filename);

  FILE* file = fopen((char*) path.data.data, "rb");
  buffer_free(&path);
  if (file == NULL) {
    fprintf(stderr, "Error opening file: %s\n", filename);
    return NULL_BYTES;
  }

  while ((bytesRead = fread(buffer, 1, 1024, file)) > 0)
    buffer_append(&data, bytes(buffer, bytesRead));

  fclose(file);
  return data.data;
}

static void verify(char* dirname, char* method, char* args) {
  char tmp[1024];

  bytes_t proof_data = {0};

  // proofer
  proofer_ctx_t*  proof_ctx = c4_proofer_create(method, args);
  data_request_t* req;
  while (proof_data.data == NULL) {
    switch (c4_proofer_execute(proof_ctx)) {
      case C4_PROOFER_WAITING:
        while ((req = c4_proofer_get_pending_data_request(proof_ctx))) {
          char test_filename[1024];
          sprintf(test_filename, "%s/%llx.%s", dirname, *((unsigned long long*) req->id), req->type == C4_DATA_TYPE_BEACON_API ? "ssz" : "json");
          bytes_t content = read_testdata(test_filename);
          TEST_ASSERT_NOT_NULL_MESSAGE(content.data, "Die not find the testdata!");
          req->response = content;
        }
        break;

      case C4_PROOFER_ERROR:
        TEST_FAIL_MESSAGE(proof_ctx->error);
        return;

      case C4_PROOFER_SUCCESS:
        proof_data = proof_ctx->proof;
        break;

      case C4_PROOFER_PENDING:
        break;
    }
  }

  // now verify
  for (int i = 0; i < 2; i++) {
    verify_ctx_t verify_ctx = {0};
    c4_verify_from_bytes(&verify_ctx, proof_ctx->proof, method, json_parse(args));

    if (verify_ctx.success) {
      c4_proofer_free(proof_ctx);
      return;
    }

    else if (!verify_ctx.first_missing_period) {
      TEST_FAIL_MESSAGE(verify_ctx.error);
      return;
    }
    else {
      char test_filename[1024];
      sprintf(test_filename, "%s/sync_data_%d.ssz", dirname, (uint32_t) verify_ctx.last_missing_period);
      bytes_t content = read_testdata(test_filename);
      TEST_ASSERT_NOT_NULL_MESSAGE(content.data, "sync_data is missing");
      c4_handle_client_updates(content);
      free(content.data);
    }
  }
  TEST_FAIL_MESSAGE("not able to verify");
}
