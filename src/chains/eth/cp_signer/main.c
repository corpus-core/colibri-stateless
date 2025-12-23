/*
 * Copyright (c) 2025 corpus.core
 *
 * SPDX-License-Identifier: MIT
 */

#include "../../../../libs/curl/http.h"
#include "bytes.h"
#include "crypto.h"
#include "eth_verify.h"
#include "json.h"
#include "state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static void c4_sleep_seconds(unsigned int seconds) {
#ifdef _WIN32
  // Sleep() expects milliseconds.
  Sleep((DWORD) seconds * 1000u);
#else
  sleep(seconds);
#endif
}

static void c4_write_status_file(const char* status_file, const char* content) {
  if (!status_file || !*status_file) return;
  FILE* f = fopen(status_file, "wb");
  if (!f) return;
  if (content && *content) {
    fwrite(content, 1, strlen(content), f);
  }
  fwrite("\n", 1, 1, f);
  fclose(f);
}

static void c4_prom_escape_label(char* out, size_t out_cap, const char* in) {
  if (!out || out_cap == 0) return;
  if (!in) {
    out[0] = 0;
    return;
  }
  size_t o = 0;
  for (const char* p = in; *p && o + 2 < out_cap; p++) {
    if (*p == '\\' || *p == '"') {
      out[o++] = '\\';
      out[o++] = *p;
    }
    else if (*p == '\n') {
      out[o++] = '\\';
      out[o++] = 'n';
    }
    else {
      out[o++] = *p;
    }
  }
  out[o] = 0;
}

static void c4_write_metrics_file(const char* metrics_file,
                                  const char* chain,
                                  uint64_t    last_signed_ts,
                                  uint64_t    last_signed_period,
                                  uint64_t    last_signed_slot,
                                  uint64_t    loop_ts,
                                  uint64_t    signed_total,
                                  uint64_t    errors_total) {
  if (!metrics_file || !*metrics_file) return;

  char chain_esc[128] = {0};
  c4_prom_escape_label(chain_esc, sizeof(chain_esc), chain ? chain : "unknown");

  // Write atomically (node_exporter textfile collector reads files concurrently).
  char tmp_path[1024] = {0};
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", metrics_file);

  FILE* f = fopen(tmp_path, "wb");
  if (!f) return;

  fprintf(f, "# HELP c4_signer_last_signed_timestamp_seconds Unix timestamp of the last successfully submitted signature batch.\n");
  fprintf(f, "# TYPE c4_signer_last_signed_timestamp_seconds gauge\n");
  fprintf(f, "c4_signer_last_signed_timestamp_seconds{chain=\"%s\"} %llu\n", chain_esc, (unsigned long long) last_signed_ts);

  fprintf(f, "# HELP c4_signer_last_signed_period Last period included in the last successfully submitted signature batch.\n");
  fprintf(f, "# TYPE c4_signer_last_signed_period gauge\n");
  fprintf(f, "c4_signer_last_signed_period{chain=\"%s\"} %llu\n", chain_esc, (unsigned long long) last_signed_period);

  fprintf(f, "# HELP c4_signer_last_signed_slot Last slot included in the last successfully submitted signature batch.\n");
  fprintf(f, "# TYPE c4_signer_last_signed_slot gauge\n");
  fprintf(f, "c4_signer_last_signed_slot{chain=\"%s\"} %llu\n", chain_esc, (unsigned long long) last_signed_slot);

  fprintf(f, "# HELP c4_signer_loop_timestamp_seconds Unix timestamp of the last signer loop iteration.\n");
  fprintf(f, "# TYPE c4_signer_loop_timestamp_seconds gauge\n");
  fprintf(f, "c4_signer_loop_timestamp_seconds{chain=\"%s\"} %llu\n", chain_esc, (unsigned long long) loop_ts);

  fprintf(f, "# HELP c4_signer_signed_total Total number of checkpoints signed by this signer process.\n");
  fprintf(f, "# TYPE c4_signer_signed_total counter\n");
  fprintf(f, "c4_signer_signed_total{chain=\"%s\"} %llu\n", chain_esc, (unsigned long long) signed_total);

  fprintf(f, "# HELP c4_signer_errors_total Total number of signer loop errors (best-effort).\n");
  fprintf(f, "# TYPE c4_signer_errors_total counter\n");
  fprintf(f, "c4_signer_errors_total{chain=\"%s\"} %llu\n", chain_esc, (unsigned long long) errors_total);

  fclose(f);

  // Best-effort atomic replace.
  (void) rename(tmp_path, metrics_file);
}

static void c4_write_status_ok(const char* status_file, const char* warn) {
  if (!status_file || !*status_file) return;
  if (warn && *warn) {
    buffer_t b = {0};
    bprintf(&b, "ok\nwarn: %s", warn);
    c4_write_status_file(status_file, (char*) b.data.data);
    buffer_free(&b);
  }
  else {
    c4_write_status_file(status_file, "ok");
  }
}

static void c4_write_status_error(const char* status_file, const char* err) {
  if (!status_file || !*status_file) return;
  if (err && *err) {
    buffer_t b = {0};
    bprintf(&b, "error\n%s", err);
    c4_write_status_file(status_file, (char*) b.data.data);
    buffer_free(&b);
  }
  else {
    c4_write_status_file(status_file, "error");
  }
}

static void c4_snip_ascii(char* out, size_t out_cap, const uint8_t* in, size_t in_len) {
  if (!out || out_cap == 0) return;
  size_t n = in_len < (out_cap - 1) ? in_len : (out_cap - 1);
  for (size_t i = 0; i < n; i++) {
    uint8_t c = in[i];
    out[i]    = (c >= 32 && c < 127) ? (char) c : '.';
  }
  out[n] = 0;
}

static void usage(const char* argv0) {
  fprintf(stderr,
          "Usage: %s --server <base_url> (--key 0x.. | --key-file <path>) [--checkpointz <url>] [--beacon-api <url>] [--status-file <path>] [--metrics-file <path>] [--chain <name>] [--max-idle <seconds>] [--once]\n"
          "\n"
          "Flow:\n"
          "  1) Derive signer address from private key\n"
          "  2) GET  /signed_checkpoints?signer=0x<address>\n"
          "  3) Verify checkpoint root is correct and finalized (Beacon API / checkpointz)\n"
          "  4) EIP-191 sign each checkpoint root\n"
          "  5) POST /signed_checkpoints  [{\"period\":...,\"signature\":\"0x...\"},...]\n",
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

static void append_json_escaped_string(buffer_t* out, const char* s) {
  buffer_add_chars(out, "\"");
  for (const char* p = s; *p; p++) {
    if (*p == '\\' || *p == '"') {
      buffer_add_chars(out, "\\");
      buffer_append(out, bytes((uint8_t*) p, 1));
    }
    else {
      buffer_append(out, bytes((uint8_t*) p, 1));
    }
  }
  buffer_add_chars(out, "\"");
}

static void maybe_set_curl_nodes_from_args(const char** checkpointz_urls, int checkpointz_count, const char** beacon_urls, int beacon_count) {
  if (checkpointz_count == 0 && beacon_count == 0) return;

  buffer_t cfg = {0};
  buffer_add_chars(&cfg, "{");
  bool first_field = true;

  if (checkpointz_count > 0) {
    if (!first_field) buffer_add_chars(&cfg, ",");
    first_field = false;
    buffer_add_chars(&cfg, "\"checkpointz\":[");
    for (int i = 0; i < checkpointz_count; i++) {
      if (i) buffer_add_chars(&cfg, ",");
      append_json_escaped_string(&cfg, checkpointz_urls[i]);
    }
    buffer_add_chars(&cfg, "]");
  }

  if (beacon_count > 0) {
    if (!first_field) buffer_add_chars(&cfg, ",");
    first_field = false;
    buffer_add_chars(&cfg, "\"beacon_api\":[");
    for (int i = 0; i < beacon_count; i++) {
      if (i) buffer_add_chars(&cfg, ",");
      append_json_escaped_string(&cfg, beacon_urls[i]);
    }
    buffer_add_chars(&cfg, "]");
  }

  buffer_add_chars(&cfg, "}");
  buffer_append(&cfg, bytes("", 1)); // NUL
  curl_set_config(json_parse((char*) cfg.data.data));
  buffer_free(&cfg);
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

static bool fetch_json_one(data_request_type_t type, char* url_owned, json_t* out_json) {
  c4_state_t      state = {0};
  data_request_t* req   = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
  req->type             = type;
  req->encoding         = C4_DATA_ENCODING_JSON;
  req->url              = url_owned; // owned by req/state
  req->method           = C4_DATA_METHOD_GET;
  c4_state_add_request(&state, req);
  curl_fetch_all(&state);
  data_request_t* done = state.requests;
  if (!done || done->error) {
    c4_state_free(&state);
    return false;
  }
  if (!done->response.data || done->response.len == 0) {
    c4_state_free(&state);
    return false;
  }

  *out_json = json_parse((char*) done->response.data);
  // Keep response memory alive: duplicate JSON slice now, then free state.
  *out_json = json_dup(*out_json);
  c4_state_free(&state);
  return out_json->type != JSON_TYPE_INVALID;
}

static bool checkpoint_root_matches_slot(uint64_t slot, const bytes32_t expected_root) {
  buffer_t url = {0};
  bprintf(&url, "eth/v1/beacon/blocks/%l/root", slot);
  json_t res = {0};
  if (!fetch_json_one(C4_DATA_TYPE_CHECKPOINTZ, (char*) url.data.data, &res)) return false;

  json_t data = json_get(res, "data");
  json_t root = json_get(data, "root");
  if (data.type != JSON_TYPE_OBJECT || root.type != JSON_TYPE_STRING) {
    safe_free((void*) res.start);
    return false;
  }

  bytes32_t got = {0};
  if (json_to_bytes(root, bytes(got, 32)) != 32) {
    safe_free((void*) res.start);
    return false;
  }

  bool ok = memcmp(got, expected_root, 32) == 0;
  safe_free((void*) res.start);
  return ok;
}

static bool checkpoint_is_finalized_by_header_root(const bytes32_t root) {
  buffer_t url = {0};
  bprintf(&url, "eth/v1/beacon/headers/0x%x", bytes(root, 32));
  json_t res = {0};
  if (!fetch_json_one(C4_DATA_TYPE_BEACON_API, (char*) url.data.data, &res)) return false;

  json_t finalized_j = json_get(res, "finalized");
  json_t data        = json_get(res, "data");
  json_t canonical_j = json_get(data, "canonical");
  json_t root_j      = json_get(data, "root");
  if (finalized_j.type != JSON_TYPE_BOOLEAN || data.type != JSON_TYPE_OBJECT || canonical_j.type != JSON_TYPE_BOOLEAN || root_j.type != JSON_TYPE_STRING) {
    safe_free((void*) res.start);
    return false;
  }

  bytes32_t got = {0};
  if (json_to_bytes(root_j, bytes(got, 32)) != 32) {
    safe_free((void*) res.start);
    return false;
  }

  bool finalized  = json_as_bool(finalized_j);
  bool canonical  = json_as_bool(canonical_j);
  bool root_match = memcmp(got, root, 32) == 0;
  safe_free((void*) res.start);
  return finalized && canonical && root_match;
}

static bool checkpoint_is_finalized_by_slot(uint64_t slot) {
  buffer_t url = {0};
  bprintf(&url, "eth/v2/beacon/blocks/%l", slot);
  json_t res = {0};
  if (!fetch_json_one(C4_DATA_TYPE_CHECKPOINTZ, (char*) url.data.data, &res)) return false;

  json_t finalized_j = json_get(res, "finalized");
  if (finalized_j.type != JSON_TYPE_BOOLEAN) {
    safe_free((void*) res.start);
    return false;
  }

  bool finalized = json_as_bool(finalized_j);
  safe_free((void*) res.start);
  return finalized;
}

int main(int argc, char** argv) {
  const char* server              = NULL;
  const char* key_hex             = NULL;
  const char* key_file            = NULL;
  bool        once                = false;
  const char* status_file         = NULL;
  const char* metrics_file        = NULL;
  const char* chain               = NULL;
  uint64_t    max_idle_seconds    = 27ull * 60ull * 60ull; // 27h
  const char* checkpointz_urls[8] = {0};
  int         checkpointz_count   = 0;
  const char* beacon_urls[8]      = {0};
  int         beacon_count        = 0;

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
    else if (strcmp(argv[i], "--checkpointz") == 0 && i + 1 < argc) {
      if (checkpointz_count < (int) (sizeof(checkpointz_urls) / sizeof(checkpointz_urls[0])))
        checkpointz_urls[checkpointz_count++] = argv[++i];
      else
        i++;
    }
    else if (strcmp(argv[i], "--beacon-api") == 0 && i + 1 < argc) {
      if (beacon_count < (int) (sizeof(beacon_urls) / sizeof(beacon_urls[0])))
        beacon_urls[beacon_count++] = argv[++i];
      else
        i++;
    }
    else if (strcmp(argv[i], "--status-file") == 0 && i + 1 < argc) {
      status_file = argv[++i];
    }
    else if (strcmp(argv[i], "--metrics-file") == 0 && i + 1 < argc) {
      metrics_file = argv[++i];
    }
    else if (strcmp(argv[i], "--chain") == 0 && i + 1 < argc) {
      chain = argv[++i];
    }
    else if (strcmp(argv[i], "--max-idle") == 0 && i + 1 < argc) {
      max_idle_seconds = strtoull(argv[++i], NULL, 10);
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

  maybe_set_curl_nodes_from_args(checkpointz_urls, checkpointz_count, beacon_urls, beacon_count);

  const time_t start_time     = time(NULL);
  time_t       last_post_time = 0;
  time_t       last_ok_time   = start_time;
  uint64_t     last_post_period = 0;
  uint64_t     last_post_slot   = 0;
  uint64_t     signed_total     = 0;
  uint64_t     errors_total     = 0;

  bytes32_t sk = {0};
  if (key_hex) {
    if (!read_key_hex(key_hex, sk)) {
      fprintf(stderr, "Invalid --key (expected 32-byte hex)\n");
      c4_write_status_error(status_file, "invalid --key");
      return 1;
    }
  }
  else {
    if (!read_key_file(key_file, sk)) {
      fprintf(stderr, "Invalid --key-file (expected 32-byte hex)\n");
      c4_write_status_error(status_file, "invalid --key-file");
      return 1;
    }
  }

  address_t addr = {0};
  if (!derive_address_from_sk(sk, addr)) {
    fprintf(stderr, "Failed to derive signer address from private key\n");
    c4_write_status_error(status_file, "failed to derive signer address");
    return 1;
  }

  buffer_t addr_buf = {0};
  bprintf(&addr_buf, "0x%x", bytes(addr, 20));
  char* addr_hex = (char*) addr_buf.data.data;

  while (true) {
    const time_t now                = time(NULL);
    const time_t last_activity_time = last_post_time ? last_post_time : start_time;
    c4_write_metrics_file(metrics_file, chain, (uint64_t) last_post_time, last_post_period, last_post_slot, (uint64_t) now, signed_total, errors_total);

    // --- GET missing checkpoints ---
    buffer_t query = {0};
    bprintf(&query, "/signed_checkpoints?signer=%s", addr_hex);
    char* get_url = join_url(server, (char*) query.data.data);
    buffer_free(&query);
    if (!get_url) {
      fprintf(stderr, "Failed to build GET url\n");
      c4_write_status_ok(status_file, "failed to build GET url");
      errors_total++;
      if (once) return 1;
      c4_sleep_seconds(60);
      continue;
    }

    c4_state_t      state     = {0};
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
      c4_write_status_ok(status_file, "failed to reach colibri server");
      errors_total++;
      c4_state_free(&state);
      if (once) return 1;
      c4_sleep_seconds(60);
      continue;
    }
    if (!req_done || !req_done->response.data || req_done->response.len == 0) {
      fprintf(stderr, "Empty HTTP GET response\n");
      c4_write_status_ok(status_file, "empty response from colibri server");
      errors_total++;
      c4_state_free(&state);
      if (once) return 1;
      c4_sleep_seconds(60);
      continue;
    }

    json_t arr = json_parse((char*) req_done->response.data);
    if (arr.type != JSON_TYPE_ARRAY) {
      char snippet[241] = {0};
      c4_snip_ascii(snippet, sizeof(snippet), req_done->response.data, req_done->response.len);
      fprintf(stderr, "Unexpected response (expected JSON array). Response snippet: %s\n", snippet);
      c4_write_status_ok(status_file, "invalid JSON from colibri server");
      errors_total++;
      c4_state_free(&state);
      if (once) return 1;
      c4_sleep_seconds(60);
      continue;
    }
    if (json_len(arr) == 0) {
      fprintf(stdout, "No checkpoints to sign for %s\n", addr_hex);
      c4_state_free(&state);
      if ((uint64_t) (now - last_activity_time) > max_idle_seconds) {
        c4_write_status_error(status_file, "no signatures posted within max-idle");
        c4_write_metrics_file(metrics_file, chain, (uint64_t) last_post_time, last_post_period, last_post_slot, (uint64_t) now, signed_total, errors_total);
      }
      else {
        c4_write_status_ok(status_file, NULL);
        last_ok_time = now;
        c4_write_metrics_file(metrics_file, chain, (uint64_t) last_post_time, last_post_period, last_post_slot, (uint64_t) now, signed_total, errors_total);
      }
      if (once) break;
      // default sleep: 1h
      fprintf(stdout, "Sleeping 3600s...\n");
      c4_sleep_seconds(3600);
      continue;
    }

    // --- Build POST payload ---
    buffer_t post = {0};
    buffer_add_chars(&post, "[");
    bool     first           = true;
    uint32_t signed_count    = 0;
    bool     had_error       = false;
    char     status_buf[256] = {0};
    uint64_t max_period_in_batch = 0;
    uint64_t max_slot_in_batch   = 0;
    json_for_each_value(arr, item) {
      uint64_t period = json_get_uint64(item, "period");
      json_t   root_j = json_get(item, "root");
      uint64_t slot   = json_get_uint64(item, "slot");
      if (root_j.type != JSON_TYPE_STRING) continue;

      bytes32_t root = {0};
      if (json_to_bytes(root_j, bytes(root, 32)) != 32) continue;

      // Verify root matches canonical root at slot, and block is finalized.
      if (!checkpoint_root_matches_slot(slot, root)) {
        fprintf(stderr, "Checkpoint root mismatch for period=%llu slot=%llu (not signing)\n", (unsigned long long) period, (unsigned long long) slot);
        snprintf(status_buf, sizeof(status_buf), "checkpoint root mismatch (period=%llu)", (unsigned long long) period);
        had_error = true;
        errors_total++;
        continue;
      }
      if (!checkpoint_is_finalized_by_header_root(root)) {
        // Fallback for checkpointz (no headers endpoint): use v2 blocks by slot which returns finalized flag.
        if (!checkpoint_is_finalized_by_slot(slot)) {
          fprintf(stdout, "Checkpoint not finalized yet for period=%llu slot=%llu (skipping)\n", (unsigned long long) period, (unsigned long long) slot);
          continue;
        }
      }

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
      signed_count++;
      if (period > max_period_in_batch) max_period_in_batch = period;
      if (slot > max_slot_in_batch) max_slot_in_batch = slot;
    }
    buffer_add_chars(&post, "]");

    if (first) {
      fprintf(stderr, "No valid checkpoints parsed from response\n");
      buffer_free(&post);
      c4_state_free(&state);
      errors_total++;
      if ((uint64_t) (now - last_activity_time) > max_idle_seconds) {
        c4_write_status_error(status_file, "no signatures posted within max-idle");
      }
      else if (had_error) {
        c4_write_status_ok(status_file, status_buf[0] ? status_buf : "failed to validate checkpoints");
      }
      else {
        c4_write_status_ok(status_file, NULL);
        last_ok_time = now;
      }
      c4_write_metrics_file(metrics_file, chain, (uint64_t) last_post_time, last_post_period, last_post_slot, (uint64_t) now, signed_total, errors_total);
      if (once) return 1;
      c4_sleep_seconds(60);
      continue;
    }

    char* post_url = join_url(server, "/signed_checkpoints");
    if (!post_url) {
      fprintf(stderr, "Failed to build POST url\n");
      buffer_free(&post);
      c4_state_free(&state);
      c4_write_status_ok(status_file, "failed to build POST url");
      errors_total++;
      c4_write_metrics_file(metrics_file, chain, (uint64_t) last_post_time, last_post_period, last_post_slot, (uint64_t) now, signed_total, errors_total);
      if (once) return 1;
      c4_sleep_seconds(60);
      continue;
    }

    // Free GET state + request first, then POST in a fresh state.
    c4_state_free(&state);

    c4_state_t      state_post     = {0};
    data_request_t* post_req_owned = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
    post_req_owned->type           = C4_DATA_TYPE_REST_API;
    post_req_owned->encoding       = C4_DATA_ENCODING_JSON;
    post_req_owned->url            = post_url;
    post_req_owned->method         = C4_DATA_METHOD_POST;
    post_req_owned->payload        = post.data; // transfer ownership to request/state
    post.data.data                 = NULL;
    post.data.len                  = 0;
    post.allocated                 = 0;
    c4_state_add_request(&state_post, post_req_owned); // ownership transferred to `state_post`
    post_req_owned = NULL;

    curl_fetch_all(&state_post);
    data_request_t* post_req_done = state_post.requests;
    if (post_req_done && post_req_done->error) {
      fprintf(stderr, "HTTP POST error: %s\n", post_req_done->error);
      c4_write_status_ok(status_file, "failed to submit signatures to colibri server");
      errors_total++;
      c4_state_free(&state_post);
      c4_write_metrics_file(metrics_file, chain, (uint64_t) last_post_time, last_post_period, last_post_slot, (uint64_t) now, signed_total, errors_total);
      if (once) return 1;
      c4_sleep_seconds(60);
      continue;
    }
    fprintf(stdout, "Posted signatures for %s\n", addr_hex);
    if (signed_count > 0) last_post_time = now;
    if (signed_count > 0) {
      signed_total += signed_count;
      last_post_period = max_period_in_batch;
      last_post_slot   = max_slot_in_batch;
    }
    c4_state_free(&state_post);

    if ((uint64_t) (now - last_activity_time) > max_idle_seconds) {
      c4_write_status_error(status_file, "no signatures posted within max-idle");
    }
    else if (had_error) {
      c4_write_status_ok(status_file, status_buf[0] ? status_buf : "checkpoint validation error");
    }
    else {
      c4_write_status_ok(status_file, NULL);
      last_ok_time = now;
    }
    c4_write_metrics_file(metrics_file, chain, (uint64_t) last_post_time, last_post_period, last_post_slot, (uint64_t) now, signed_total, errors_total);

    if (once) break;
    fprintf(stdout, "Sleeping 3600s...\n");
    c4_sleep_seconds(3600);
  }

  buffer_free(&addr_buf);
  return 0;
}
