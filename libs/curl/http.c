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

#include "http.h"
#include "logger.h"
#include "state.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#include "../../src/util/win_compat.h"
#else
#include <unistd.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

typedef struct {
  json_t config;
} curl_config_t;

typedef struct {
  data_request_t* request;
  CURL*           curl;
  buffer_t        buffer;
  char*           url;
} curl_request_t;

static curl_nodes_t curl_nodes     = {0};
const char*         CURL_METHODS[] = {"GET", "POST", "PUT", "DELETE"};

#define DEFAULT_CONFIG "{\"eth_rpc\":[\"https://nameless-sly-reel.quiknode.pro/5937339c28c09a908994b74e2514f0f6cfdac584\",\"https://ethereum-mainnet.core.chainstack.com/364e0a05996fe175eb1975ddc6e9147d\",\"https://nameless-sly-reel.quiknode.pro/5937339c28c09a908994b74e2514f0f6cfdac584/\",\"https://eth-mainnet.g.alchemy.com/v2/B8W2IZrDkCkkjKxQOl70XNIy4x4PT20S\",\"https://rpc.ankr.com/eth/c14449317accec005863d22c7515f6b69667abb29ba2b5e099abf490bcb875b1\",\"https://eth.llamarpc.com\",\"https://rpc.payload.de\",\"https://ethereum-rpc.publicnode.com\"]," \
                       "\"beacon_api\":[\"https://ethereum-mainnet.core.chainstack.com/beacon/364e0a05996fe175eb1975ddc6e9147d/\",\"http://unstable.mainnet.beacon-api.nimbus.team/\",\"https://lodestar-mainnet.chainsafe.io/\"],"                                                                                                                                                                                                                                                                                                                                         \
                       "\"checkpointz\":[\"https://sync-mainnet.beaconcha.in\",\"https://beaconstate.info\",\"https://sync.invis.tools\",\"https://beaconstate.ethstaker.cc\"]}"
static void
init_config() {
  if (!bytes_all_zero(bytes(&curl_nodes, sizeof(curl_nodes)))) return;
  json_t config          = json_parse(DEFAULT_CONFIG);
  curl_nodes.eth_rpc     = json_dup(json_get(config, "eth_rpc"));
  curl_nodes.beacon_api  = json_dup(json_get(config, "beacon_api"));
  curl_nodes.checkpointz = json_dup(json_get(config, "checkpointz"));
  curl_nodes.prover      = json_dup(json_get(config, "prover"));

  char*   config_file = getenv("C4_CONFIG");
  bytes_t content     = {0};
  if (config_file) content = bytes_read(config_file);
  if (!content.data) content = bytes_read("c4_config.json");
  if (content.data) {
    curl_set_config(json_parse((char*) content.data));
    safe_free(content.data);
  }
}

static void replace_config(json_t* old_config, json_t new_config) {
  if (old_config->start) safe_free((void*) old_config->start);
  *old_config = json_dup(new_config);
}

void curl_set_config(json_t config) {
  init_config();
  json_t nodes = {0};

  if ((nodes = json_get(config, "eth_rpc")).type == JSON_TYPE_ARRAY) replace_config(&curl_nodes.eth_rpc, nodes);
  if ((nodes = json_get(config, "beacon_api")).type == JSON_TYPE_ARRAY) replace_config(&curl_nodes.beacon_api, nodes);
  if ((nodes = json_get(config, "checkpointz")).type == JSON_TYPE_ARRAY) replace_config(&curl_nodes.checkpointz, nodes);
  if ((nodes = json_get(config, "prover")).type == JSON_TYPE_ARRAY) replace_config(&curl_nodes.prover, nodes);
  if ((nodes = json_get(config, "chain_store")).type == JSON_TYPE_ARRAY) replace_config(&curl_nodes.chain_store, nodes);
}

static json_t get_nodes(data_request_type_t type) {
  init_config();

  switch (type) {
    case C4_DATA_TYPE_ETH_RPC:
      return curl_nodes.eth_rpc;
    case C4_DATA_TYPE_BEACON_API:
      return curl_nodes.beacon_api;
    case C4_DATA_TYPE_CHECKPOINTZ:
      return curl_nodes.checkpointz;
    case C4_DATA_TYPE_PROVER:
      return curl_nodes.prover;
    case C4_DATA_TYPE_INTERN:
      return curl_nodes.chain_store;
    default:
      return (json_t) {0};
  }
}

char*       cache_dir = NULL;
static void curl_request_free(curl_request_t* creq) {
  safe_free(creq->url);
  buffer_free(&creq->buffer);
  curl_easy_cleanup(creq->curl);
}

void curl_set_cache_dir(const char* dir) {
  cache_dir = strdup(dir);
  MKDIR(cache_dir);
}

#define return_error(req, msg) \
  {                            \
    req->error = strdup(msg);  \
    return false;              \
  }

#ifdef TEST
static char* REQ_TEST_DIR = NULL;
static void  test_write_file(const char* filename, bytes_t data) {
  if (!REQ_TEST_DIR) return;
  buffer_t buf = {0};
  bytes_write(data, fopen(bprintf(&buf, "%s/%s", REQ_TEST_DIR, filename), "w"), true);
  buffer_free(&buf);
}

char* curl_set_test_dir(const char* dir) {
  buffer_t buf = {0};
  REQ_TEST_DIR = bprintf(&buf, "%s/%s", TESTDATA_DIR, dir);
  MKDIR(REQ_TEST_DIR);
  setenv("C4_STATES_DIR", REQ_TEST_DIR, true);
  return REQ_TEST_DIR;
  //  if (MKDIR(REQ_TEST_DIR) != 0) perror("Error creating directory");
}
#endif

static size_t curl_append(void* contents, size_t size, size_t nmemb, void* buf) {
  buffer_t* buffer = (buffer_t*) buf;
  buffer_grow(buffer, buffer->data.len + size * nmemb + 1);
  buffer_append(buffer, bytes(contents, size * nmemb));
  buffer->data.data[buffer->data.len] = '\0';
  return size * nmemb;
}

#ifdef TEST
static bool check_cache(data_request_t* req) {
  buffer_t buf = {0};
  bprintf(&buf, "%s/%s", cache_dir, c4_req_mockname(req));
  bytes_t content = bytes_read((char*) buf.data.data);
  if (content.data) {
    req->response = content;
    return true;
  }
  return false;
}

static void write_cache(data_request_t* req) {
  buffer_t buf = {0};
  bprintf(&buf, "%s/%s", cache_dir, c4_req_mockname(req));
  bytes_write(req->response, fopen((char*) buf.data.data, "w"), true);
  buffer_free(&buf);
}

#endif

static bool configure_request(curl_request_t* creq) {
  data_request_t* req = creq->request;

#ifdef TEST
  if (cache_dir && check_cache(req)) return false;

#endif
  json_t servers = get_nodes(req->type);

  if (req->type != C4_DATA_TYPE_REST_API && servers.type != JSON_TYPE_ARRAY) return_error(req, "Invalid servers in config");
  int i = 0;
  if (req->type == C4_DATA_TYPE_REST_API) {
    if (req->response_node_index) return_error(req, "Failed request");
    req->response_node_index = 1;
    creq->url                = strdup(req->url);
    return true;
  }
  else
    json_for_each_value(servers, server) {
      if (req->response_node_index > i || req->node_exclude_mask & (1 << (i + 1))) {
        i++;
        continue;
      }
      req->response_node_index = i + 1;
      buffer_t url             = {0};
      bprintf(&url, "%j", server);
      if (req->url && *req->url) {
        if (url.data.len > 0 && url.data.data[url.data.len - 1] != '/') buffer_add_chars(&url, "/");
        if (req->type == C4_DATA_TYPE_INTERN && strncmp(req->url, "chain_store/", 12) == 0)
          buffer_add_chars(&url, req->url + 12);
        else
          buffer_add_chars(&url, req->url);
      }
      creq->url = (char*) url.data.data;
      return true;
    }
  return_error(req, "Failed request, no more nodes to try");
}

static bool configure_curl(curl_request_t* creq) {
  data_request_t* req = creq->request;
  if (req->response.data || !creq->url) return false;
  if (req->error) {
    safe_free(req->error);
    req->error = NULL;
  }
  if (req->payload.len && req->payload.data)
    log_info("req: %s : %j", creq->url, (json_t) {.start = (char*) req->payload.data, .len = req->payload.len, .type = JSON_TYPE_OBJECT});
  else
    log_info("req: %s", creq->url);

  creq->curl = curl_easy_init();
  if (!creq->curl) return_error(req, "Failed to initialize curl");

  curl_easy_setopt(creq->curl, CURLOPT_URL, creq->url);
  if (req->payload.len && req->payload.data) {
    curl_easy_setopt(creq->curl, CURLOPT_POSTFIELDS, req->payload.data);
    curl_easy_setopt(creq->curl, CURLOPT_POSTFIELDSIZE, (long) req->payload.len);
  }

  struct curl_slist* headers = NULL;
  headers                    = curl_slist_append(headers, req->encoding == C4_DATA_ENCODING_JSON ? "Accept: application/json" : "Accept: application/octet-stream");
  if (req->payload.len && req->payload.data)
    headers = curl_slist_append(headers, req->encoding == C4_DATA_ENCODING_JSON ? "Content-Type: application/json" : "Content-Type: application/octet-stream");
  headers = curl_slist_append(headers, "charsets: utf-8");
  headers = curl_slist_append(headers, "User-Agent: c4 curl ");
  curl_easy_setopt(creq->curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(creq->curl, CURLOPT_WRITEFUNCTION, curl_append);
  curl_easy_setopt(creq->curl, CURLOPT_WRITEDATA, &creq->buffer);
  curl_easy_setopt(creq->curl, CURLOPT_TIMEOUT, (uint64_t) 120);
  curl_easy_setopt(creq->curl, CURLOPT_CUSTOMREQUEST, CURL_METHODS[req->method]);
  return true;
}

static bool curl_execute(curl_request_t* creq) {
  CURLcode res = curl_easy_perform(creq->curl);
  if (res == CURLE_OK) {
    creq->request->response = creq->buffer.data;
    creq->buffer            = (buffer_t) {0};
  }
  else
    creq->request->error = bprintf(NULL, "%s : %s", curl_easy_strerror(res), bprintf(&creq->buffer, " "));
  return res == CURLE_OK;
}

static bool curl_handle(curl_request_t* creq) {
  char* last_error = NULL;
  while (true) {
    if (!configure_request(creq) || !configure_curl(creq)) break;

    if (curl_execute(creq)) break;
    // remove the last error and try again
    if (last_error) safe_free(last_error);
    last_error           = creq->request->error;
    creq->request->error = NULL;
    curl_request_free(creq);
  }

  // combine errors, if we have a last_error
  if (creq->request->error && last_error) {
    char* src            = creq->request->error;
    creq->request->error = bprintf(NULL, "%s : %s", src, last_error);
    safe_free(src);
    safe_free(last_error);
  }

#ifdef TEST
  if (creq->request->response.data && REQ_TEST_DIR) {
    char* test_filename = c4_req_mockname(creq->request);
    test_write_file(test_filename, creq->request->response);
    safe_free(test_filename);
  }
  if (cache_dir && creq->request->response.data) write_cache(creq->request);
#endif
  return creq->request->response.data != NULL;
}

void curl_fetch(data_request_t* req) {
  curl_request_t creq = {.request = req};
  curl_handle(&creq);
  curl_request_free(&creq);
}

void curl_fetch_all(c4_state_t* state) {
  bool retry = true;

  while (retry) {
    retry               = false;
    CURLM* multi_handle = curl_multi_init();
    if (!multi_handle) return;

    int running_handles = 0;
    int request_count   = 0;

    // Set up all requests that need processing and add them to the multi handle
    for (data_request_t* req = state->requests; req; req = req->next) {
      if (req->response.data) continue; // Skip if already has response

      curl_request_t* creq = (curl_request_t*) safe_calloc(1, sizeof(curl_request_t));
      if (!creq) continue;
      creq->request = req;

      if (!configure_request(creq)) {
        safe_free(creq);
        continue;
      }

      if (!configure_curl(creq)) {
        curl_request_free(creq);
        safe_free(creq);
        continue;
      }

      curl_multi_add_handle(multi_handle, creq->curl);
      curl_easy_setopt(creq->curl, CURLOPT_PRIVATE, creq);
      request_count++;
    }

    // Only proceed if we have requests to process
    if (request_count > 0) {
      // Start the transfers
      curl_multi_perform(multi_handle, &running_handles);

      // Wait for all transfers to complete
      while (running_handles) {
        int       numfds = 0;
        CURLMcode mc     = curl_multi_wait(multi_handle, NULL, 0, 1000, &numfds);
        if (mc != CURLM_OK) break;

        // Check if any transfers are complete
        curl_multi_perform(multi_handle, &running_handles);
      }

      // Process results
      CURLMsg* message;
      int      pending;

      while ((message = curl_multi_info_read(multi_handle, &pending))) {
        if (message->msg == CURLMSG_DONE) {
          CURL*           easy_handle = message->easy_handle;
          curl_request_t* creq        = NULL;

          curl_easy_getinfo(easy_handle, CURLINFO_PRIVATE, &creq);

          if (creq) {

            long response_code;
            curl_easy_getinfo(easy_handle, CURLINFO_RESPONSE_CODE, &response_code);

            if (message->data.result == CURLE_OK && response_code >= 200 && response_code < 300) {
              // Success case
              creq->request->response = creq->buffer.data;
              creq->buffer            = (buffer_t) {0};
#ifdef TEST
              if (creq->request->response.data && REQ_TEST_DIR) {
                char* test_filename = c4_req_mockname(creq->request);
                test_write_file(test_filename, creq->request->response);
                safe_free(test_filename);
              }
              if (cache_dir && creq->request->response.data) write_cache(creq->request);
#endif
            }
            else {
              // Error case - store error but mark for retry if possible
              char* error_msg = bprintf(NULL, "%s : %s", message->data.result == CURLE_OK ? "" : curl_easy_strerror(message->data.result), bprintf(&creq->buffer, " "));

              // Check if we need to keep retrying with different nodes
              if (creq->request->type != C4_DATA_TYPE_REST_API) {
                json_t servers = get_nodes(creq->request->type);

                // Calculate if we have more nodes to try
                int node_count = 0;
                json_for_each_value(servers, server) {
                  node_count++;
                }

                // If we have more nodes to try
                if (creq->request->response_node_index < node_count) {
                  // Store this error as the last error
                  if (creq->request->error) safe_free(creq->request->error);
                  creq->request->error = error_msg;

                  // We need to retry
                  retry = true;
                }
                else {
                  // No more nodes to try, set final error
                  creq->request->error = error_msg;
                }
              }
              else {
                // REST API doesn't retry with different nodes
                creq->request->error = error_msg;
              }
            }

            curl_multi_remove_handle(multi_handle, easy_handle);
            safe_free(creq->url);
            buffer_free(&creq->buffer);
            curl_easy_cleanup(easy_handle);
            safe_free(creq);
          }
        }
      }
    }

    curl_multi_cleanup(multi_handle);
  }
}
