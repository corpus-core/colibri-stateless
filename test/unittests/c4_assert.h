#include "beacon_types.h"
#include "bytes.h"
#include "chains.h"
#include "crypto.h"
#include "json.h"
#include "plugin.h"
#include "proofer.h"
#include "ssz.h"
#include "state.h"
#include "sync_committee.h"
#include "unity.h"
#include "verify.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define C4_PROOFER_FLAG_NO_CACHE (1 << 30)
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
      .max_sync_states = 3};
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
    //    fprintf(stderr, "Error opening file: %s\n", filename);
    return NULL_BYTES;
  }

  while ((bytesRead = fread(buffer, 1, 1024, file)) > 0)
    buffer_append(&data, bytes(buffer, bytesRead));

  fclose(file);
  return data.data;
}

static void set_state(chain_id_t chain_id, char* dirname) {
  char dir_path[2024];
  sprintf(dir_path, "%s/%s", TESTDATA_DIR, dirname);

  DIR* dir = opendir(dir_path);
  if (!dir) return;

  struct dirent* entry;
  while ((entry = readdir(dir)) != NULL) {
    char* filename = entry->d_name;

    // Skip files containing a period
    if (strchr(filename, '.') != NULL) continue;

    // Skip . and .. directory entries
    if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) continue;

    // Read the file content - read_testdata already adds TESTDATA_DIR
    char rel_path[1024];
    sprintf(rel_path, "%s/%s", dirname, filename);
    bytes_t content = read_testdata(rel_path);

    if (content.data) {
      // Store in file cache
      file_set(filename, content);
      free(content.data);
    }
  }

  closedir(dir);
}
static void verify_count(char* dirname, char* method, char* args, chain_id_t chain_id, size_t count, proofer_flags_t flags, char* expected_result) {
  char tmp[1024];

  if ((flags & C4_PROOFER_FLAG_NO_CACHE) == 0)
    set_state(chain_id, dirname);

  bytes_t proof_data = {0};

  // proofer
  proofer_ctx_t*  proof_ctx = c4_proofer_create(method, args, chain_id, flags);
  data_request_t* req;
  while (proof_data.data == NULL) {
    switch (c4_proofer_execute(proof_ctx)) {
      case C4_PENDING:
        while ((req = c4_state_get_pending_request(&proof_ctx->state))) {
          char* filename = c4_req_mockname(req);
          sprintf(tmp, "%s/%s", dirname, filename);
          free(filename);
          //          printf("read : %s\n     %s\n", tmp, req->payload.data ? (char*) req->payload.data : "");
          bytes_t content = read_testdata(tmp);
          TEST_ASSERT_NOT_NULL_MESSAGE(content.data, bprintf(NULL, "Did not find the testdata: %s", tmp));
          req->response = content;
        }
        break;

      case C4_ERROR:
        TEST_FAIL_MESSAGE(proof_ctx->state.error);
        return;

      case C4_SUCCESS:
        proof_data = proof_ctx->proof;
        //        ssz_dump_to_file(stdout, (ssz_ob_t) {.def = eth_ssz_verification_type(ETH_SSZ_VERIFY_REQUEST), .bytes = proof_data}, true, true);
        break;
    }
  }

  for (int n = 0; n < count; n++) {
    // now verify
    bool         success    = false;
    verify_ctx_t verify_ctx = {0};
    for (int i = 0; i < 10; i++) {
      c4_status_t status = i == 0 ? c4_verify_from_bytes(&verify_ctx, proof_ctx->proof, method, json_parse(args), chain_id) : c4_verify(&verify_ctx);
      if (status == C4_PENDING) {
        for (data_request_t* req = c4_state_get_pending_request(&verify_ctx.state); req; req = c4_state_get_pending_request(&verify_ctx.state)) {
          char* filename = c4_req_mockname(req);
          sprintf(tmp, "%s/%s", dirname, filename);
          free(filename);
          //          printf("read : %s\n     %s", tmp, req->payload.data);
          bytes_t content = read_testdata(tmp);
          TEST_ASSERT_NOT_NULL_MESSAGE(content.data, bprintf(NULL, "Did not find the testdata: %s", tmp));
          req->response = content;
        }
        continue;
      }

      if (verify_ctx.success) {
        if (expected_result) {
          char* result = ssz_dump_to_str(verify_ctx.data, false, true);
          TEST_ASSERT_EQUAL_STRING_MESSAGE(expected_result, result, "wrong result");
          free(result);
        }
        success = true;
        break;
      }
      else if (status == C4_ERROR) {
        TEST_FAIL_MESSAGE(verify_ctx.state.error);
        break;
      }
    }
    TEST_ASSERT_TRUE_MESSAGE(success, "not able to verify"); //    TEST_FAIL_MESSAGE("not able to verify");
    c4_verify_free_data(&verify_ctx);
  }
  c4_proofer_free(proof_ctx);
}

static void verify(char* dirname, char* method, char* args, chain_id_t chain_id) {
  verify_count(dirname, method, args, chain_id, 1, C4_PROOFER_FLAG_INCLUDE_CODE, NULL);
}

static void run_rpc_test(char* dirname, proofer_flags_t flags) {
  char test_filename[1024];
  sprintf(test_filename, "%s/test.json", dirname);
  bytes_t    test_content    = read_testdata(test_filename);
  json_t     test            = json_parse((char*) test_content.data);
  char*      method          = bprintf(NULL, "%j", json_get(test, "method"));
  char*      args            = json_new_string(json_get(test, "params"));
  chain_id_t chain_id        = (chain_id_t) json_get_uint64(test, "chain_id");
  char*      expected_result = bprintf(NULL, "%J", json_get(test, "expected_result"));

  verify_count(dirname, method, args, C4_CHAIN_MAINNET, 1, flags, expected_result);

  free(method);
  free(args);
  free(expected_result);
  free(test_content.data);
}
