#include "../util/bytes.h"
#include "../util/crypto.h"
#include "../util/request.h"
#include "../util/ssz.h"
#include "../verifier/types_beacon.h"
#include "../verifier/types_verify.h"
#include "../verifier/verify.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef USE_CURL
#include "../../libs/curl/http.h"
#endif

#ifdef TEST
static char* REQ_TEST_DIR = NULL;

static void set_req_test_dir(const char* dir) {
  char* path = malloc(strlen(dir) + strlen(TESTDATA_DIR) + 5);
  sprintf(path, "%s/%s", TESTDATA_DIR, dir);
  REQ_TEST_DIR = path;
}

#endif

void error(const char* msg) {
  fprintf(stderr, "%s\n", msg);
  exit(EXIT_FAILURE);
}
#ifdef USE_CURL
static bool get_client_updates(verify_ctx_t* ctx) {
  char url[200] = {0};
  sprintf(url, "eth/v1/beacon/light_client/updates?start_period=%d&count=%d", (uint32_t) ctx->first_missing_period - 1, (uint32_t) (ctx->last_missing_period - ctx->first_missing_period + 1));
  data_request_t req = {
      .encoding = C4_DATA_ENCODING_SSZ,
      .error    = NULL,
      .id       = {0},
      .method   = C4_DATA_METHOD_GET,
      .payload  = {0},
      .response = {0},
      .type     = C4_DATA_TYPE_BEACON_API,
      .url      = url,
      .chain_id = ctx->chain_id};

  sha256(bytes((uint8_t*) url, strlen(url)), req.id);

  curl_fetch(&req);
  if (req.error) {
    fprintf(stderr, "Error fetching client updates: %s\n", req.error);
    return false;
  }

#ifdef TEST
  if (REQ_TEST_DIR) {

    char test_filename[1024];
    sprintf(test_filename, "%s/sync_data_%d.ssz", REQ_TEST_DIR, (uint32_t) ctx->last_missing_period);
    bytes_write(req.response, fopen(test_filename, "wb"), true);
  }
#endif

  buffer_t updates = {0};
  if (req.response.len && req.response.data[0] == '{') {
    json_t json = json_parse((char*) req.response.data);
    json_t msg  = json_get(json, "message");

    fprintf(stderr, "Error fetching updates: %s\n", json_as_string(msg, &updates));
    exit(EXIT_FAILURE);
  }

  uint32_t pos    = 0;
  uint32_t period = ctx->first_missing_period;
  while (pos < req.response.len) {
    fprintf(stderr, "## verifying period %d\n", period++);
    updates.data.len = 0;
    uint64_t length  = uint64_from_le(req.response.data + pos);
    buffer_grow(&updates, length + 100);
    buffer_append(&updates, bytes(NULL, 15)); // 3 offsets + 3 union bytes
    uint64_to_le(updates.data.data, 12);      // offset for data
    uint64_to_le(updates.data.data + 4, 13);  // offset for proof
    uint64_to_le(updates.data.data + 8, 14);  // offset for sync
    updates.data.data[14] = 1;                // union type for lightclient updates

    ssz_builder_t builder = {0};
    builder.def           = (ssz_def_t*) (C4_REQUEST_SYNCDATA_UNION + 1); // union type for lightclient updates
    ssz_add_dynamic_list_bytes(&builder, 1, bytes(req.response.data + pos + 8 + 4, length - 4));
    bytes_t list_data = ssz_builder_to_bytes(&builder).bytes;
    buffer_append(&updates, list_data);
    free(list_data.data);

    verify_ctx_t sync_ctx = {0};
    c4_verify_from_bytes(&sync_ctx, updates.data, NULL, (json_t) {0}, ctx->chain_id);
    if (sync_ctx.error) {
      if (sync_ctx.last_missing_period && sync_ctx.first_missing_period != ctx->first_missing_period)
        return get_client_updates(&sync_ctx);

      fprintf(stderr, "Error verifying sync data: %s\n", sync_ctx.error);
      return false;
    }

    pos += length + 8;
  }

  buffer_free(&updates);

  // each entry:
  //  - 8 bytes (uint64) length
  //- 4 bytes forDigest
  //- LightClientUpdate

  // wrap into request
  return true;
}
#endif

int main(int argc, char* argv[]) {
  if (argc == 1) {
    fprintf(stderr, "Usage: %s request.ssz \n", argv[0]);
    exit(EXIT_FAILURE);
  }

  char*      method   = NULL;
  chain_id_t chain_id = C4_CHAIN_MAINNET;
  buffer_t   args     = {0};
  char*      input    = NULL;
  buffer_add_chars(&args, "[");

  for (int i = 1; i < argc; i++) {
    if (*argv[i] == '-') {
      for (char* c = argv[i] + 1; *c; c++) {
        switch (*c) {
          case 'c':
            chain_id = atoi(argv[++i]);
            break;
#ifdef TEST
          case 't':
            set_req_test_dir(argv[++i]);
            break;
#endif
          default:
            fprintf(stderr, "Unknown option: %c\n", *c);
            exit(EXIT_FAILURE);
        }
      }
      if (input == NULL && strlen(argv[i]) == 1)
        input = argv[i];
    }
    else if (input == NULL)
      input = argv[i];
    else if (method == NULL)
      method = argv[i];
    else {
      if (args.data.len > 1) buffer_add_chars(&args, ",");
      bprintf(&args, "\"%s\"", argv[i]);
    }
  }
  buffer_add_chars(&args, "]");
  if (input == NULL) {
    fprintf(stderr, "No input file provided\n");
    exit(EXIT_FAILURE);
  }
  bytes_t request = bytes_read(input);

  for (int i = 0; i < 5; i++) { // max 5 retries

    verify_ctx_t ctx = {0};
    c4_verify_from_bytes(&ctx, request, method, method ? json_parse((char*) args.data.data) : (json_t) {0}, chain_id);

    if (ctx.success) {
      ssz_dump_to_file(stdout, ctx.data, false, true);
      fflush(stdout);
      return EXIT_SUCCESS;
    }
    else {

      if (ctx.first_missing_period) printf("first missing period: %" PRIu64 "\n", ctx.first_missing_period);
      if (ctx.last_missing_period) printf("last missing period: %" PRIu64 "\n", ctx.last_missing_period);
// getting the client updates
#ifdef USE_CURL
      if (!ctx.first_missing_period || !get_client_updates(&ctx)) {
        fprintf(stderr, "proof is invalid: %s\n", ctx.error);
        return EXIT_FAILURE;
      }
#else
      fprintf(stderr, "proof is invalid: %s\n", ctx.error);
      return EXIT_FAILURE;
#endif
    }
  }
  return EXIT_FAILURE;
}