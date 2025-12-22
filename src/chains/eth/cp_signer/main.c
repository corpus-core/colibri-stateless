/*
 * Copyright (c) 2025 corpus.core
 *
 * SPDX-License-Identifier: MIT
 */

#include "bytes.h"
#include "crypto.h"
#include "eth_verify.h"
#include "json.h"
#include "state.h"
#include "../../../../libs/curl/http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char* argv0) {
  fprintf(stderr,
          "Usage: %s --server <base_url> (--key 0x.. | --key-file <path>) [--once]\n"
          "\n"
          "Flow:\n"
          "  1) Derive signer address from private key\n"
          "  2) GET  /signed_checkpoints?signer=0x<address>\n"
          "  3) EIP-191 sign each checkpoint root\n"
          "  4) POST /signed_checkpoints  [{\"period\":...,\"signature\":\"0x...\"},...]\n",
          argv0);
}

static bool read_key_hex(const char* hex, bytes32_t out_sk) {
  if (!hex) return false;
  memset(out_sk, 0, 32);
  int n = hex_to_bytes((char*) hex, -1, bytes(out_sk, 32));
  return n == 32;
}

static bool read_key_file(const char* path, bytes32_t out_sk) {
  bytes_t content = bytes_read((char*) path);
  if (!content.data) return false;
  // trim whitespace
  buffer_t tmp = {0};
  for (uint32_t i = 0; i < content.len; i++) {
    unsigned char c = content.data[i];
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
      buffer_append(&tmp, bytes(&c, 1));
  }
  buffer_append(&tmp, bytes("", 1)); // NUL
  bool ok = read_key_hex((char*) tmp.data.data, out_sk);
  buffer_free(&tmp);
  safe_free(content.data);
  return ok;
}

static bool derive_address_from_sk(const bytes32_t sk, address_t out_addr) {
  // Derive address via sign+recover on a fixed digest.
  bytes32_t digest = {0};
  bytes32_t tmp    = {0};
  uint8_t   sig[65];
  uint8_t   pub[64];

  // digest = keccak256("colibri-checkpoint-signer")
  const char* msg = "colibri-checkpoint-signer";
  keccak(bytes((uint8_t*) msg, (uint32_t) strlen(msg)), digest);

  if (!secp256k1_sign(sk, digest, sig)) return false;
  if (!secp256k1_recover(digest, bytes(sig, 65), pub)) return false;

  keccak(bytes(pub, 64), tmp);
  memcpy(out_addr, tmp + 12, 20);
  return !bytes_all_zero(bytes(out_addr, 20));
}

static char* join_url(const char* base, const char* path_and_query) {
  buffer_t buf = {0};
  if (!base || !path_and_query) return NULL;
  buffer_add_chars(&buf, base);
  // Ensure exactly one slash between base and path
  size_t bl = strlen(base);
  if (bl == 0) {
    buffer_free(&buf);
    return NULL;
  }
  if (base[bl - 1] == '/' && path_and_query[0] == '/')
    buffer_add_chars(&buf, path_and_query + 1);
  else if (base[bl - 1] != '/' && path_and_query[0] != '/')
    buffer_add_chars(&buf, "/"), buffer_add_chars(&buf, path_and_query);
  else
    buffer_add_chars(&buf, path_and_query);

  // NUL terminate
  buffer_append(&buf, bytes("", 1));
  return (char*) buf.data.data; // caller owns
}

int main(int argc, char** argv) {
  const char* server   = NULL;
  const char* key_hex  = NULL;
  const char* key_file = NULL;
  bool        once     = false;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--server") == 0 && i + 1 < argc) {
      server = argv[++i];
    }
    else if (strcmp(argv[i], "--key") == 0 && i + 1 < argc) {
      key_hex = argv[++i];
    }
    else if (strcmp(argv[i], "--key-file") == 0 && i + 1 < argc) {
      key_file = argv[++i];
    }
    else if (strcmp(argv[i], "--once") == 0) {
      once = true;
    }
    else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage(argv[0]);
      return 0;
    }
    else {
      fprintf(stderr, "Unknown arg: %s\n", argv[i]);
      usage(argv[0]);
      return 1;
    }
  }

  if (!server || (!key_hex && !key_file)) {
    usage(argv[0]);
    return 1;
  }

  bytes32_t sk = {0};
  if (key_hex) {
    if (!read_key_hex(key_hex, sk)) {
      fprintf(stderr, "Invalid --key (expected 32-byte hex)\n");
      return 1;
    }
  }
  else {
    if (!read_key_file(key_file, sk)) {
      fprintf(stderr, "Invalid --key-file (expected 32-byte hex)\n");
      return 1;
    }
  }

  address_t addr = {0};
  if (!derive_address_from_sk(sk, addr)) {
    fprintf(stderr, "Failed to derive signer address from private key\n");
    return 1;
  }

  buffer_t addr_buf = {0};
  bprintf(&addr_buf, "0x%x", bytes(addr, 20));
  char* addr_hex = (char*) addr_buf.data.data;

  while (true) {
    // --- GET missing checkpoints ---
    buffer_t query = {0};
    bprintf(&query, "/signed_checkpoints?signer=%s", addr_hex);
    char* get_url = join_url(server, (char*) query.data.data);
    buffer_free(&query);
    if (!get_url) {
      fprintf(stderr, "Failed to build GET url\n");
      return 1;
    }

    c4_state_t state = {0};
    data_request_t* req_owned = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
    req_owned->type           = C4_DATA_TYPE_REST_API;
    req_owned->encoding       = C4_DATA_ENCODING_JSON;
    req_owned->url            = get_url;
    req_owned->method         = C4_DATA_METHOD_GET;
    c4_state_add_request(&state, req_owned); // ownership transferred to `state`
    req_owned = NULL;
    curl_fetch_all(&state);
    data_request_t* req_done = state.requests;
    if (req_done && req_done->error) {
      fprintf(stderr, "HTTP GET error: %s\n", req_done->error);
      c4_state_free(&state);
      return 1;
    }
    if (!req_done || !req_done->response.data || req_done->response.len == 0) {
      fprintf(stderr, "Empty HTTP GET response\n");
      c4_state_free(&state);
      return 1;
    }

    json_t arr = json_parse((char*) req_done->response.data);
    if (arr.type != JSON_TYPE_ARRAY) {
      fprintf(stderr, "Unexpected response (expected JSON array)\n");
      c4_state_free(&state);
      return 1;
    }
    if (json_len(arr) == 0) {
      fprintf(stdout, "No checkpoints to sign for %s\n", addr_hex);
      c4_state_free(&state);
      if (once) break;
      // default sleep: 1h
      fprintf(stdout, "Sleeping 3600s...\n");
      sleep(3600);
      continue;
    }

    // --- Build POST payload ---
    buffer_t post = {0};
    buffer_add_chars(&post, "[");
    bool first = true;
    json_for_each_value(arr, item) {
      uint64_t period = json_get_uint64(item, "period");
      json_t   root_j = json_get(item, "root");
      if (root_j.type != JSON_TYPE_STRING) continue;

      bytes32_t root = {0};
      if (json_to_bytes(root_j, bytes(root, 32)) != 32) continue;

      bytes32_t digest = {0};
      uint8_t   sig[65];
      c4_eth_eip191_digest_32(root, digest);
      if (!secp256k1_sign(sk, digest, sig)) continue;

      buffer_t sig_buf = {0};
      bprintf(&sig_buf, "0x%x", bytes(sig, 65));
      char* sig_hex = (char*) sig_buf.data.data;

      if (!first) buffer_add_chars(&post, ",");
      first = false;
      bprintf(&post, "{\"period\":%l,\"signature\":\"%s\"}", period, sig_hex);
      buffer_free(&sig_buf);
    }
    buffer_add_chars(&post, "]");

    if (first) {
      fprintf(stderr, "No valid checkpoints parsed from response\n");
      buffer_free(&post);
      c4_state_free(&state);
      return 1;
    }

    char* post_url = join_url(server, "/signed_checkpoints");
    if (!post_url) {
      fprintf(stderr, "Failed to build POST url\n");
      buffer_free(&post);
      c4_state_free(&state);
      return 1;
    }

    // Free GET state + request first, then POST in a fresh state.
    c4_state_free(&state);

    c4_state_t state_post = {0};
    data_request_t* post_req_owned = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
    post_req_owned->type           = C4_DATA_TYPE_REST_API;
    post_req_owned->encoding       = C4_DATA_ENCODING_JSON;
    post_req_owned->url            = post_url;
    post_req_owned->method         = C4_DATA_METHOD_POST;
    post_req_owned->payload        = post.data; // transfer ownership to request/state
    post.data.data           = NULL;
    post.data.len            = 0;
    post.allocated           = 0;
    c4_state_add_request(&state_post, post_req_owned); // ownership transferred to `state_post`
    post_req_owned = NULL;

    curl_fetch_all(&state_post);
    data_request_t* post_req_done = state_post.requests;
    if (post_req_done && post_req_done->error) {
      fprintf(stderr, "HTTP POST error: %s\n", post_req_done->error);
      c4_state_free(&state_post);
      return 1;
    }
    fprintf(stdout, "Posted signatures for %s\n", addr_hex);
    c4_state_free(&state_post);

    if (once) break;
    fprintf(stdout, "Sleeping 3600s...\n");
    sleep(3600);
  }

  buffer_free(&addr_buf);
  return 0;
}
