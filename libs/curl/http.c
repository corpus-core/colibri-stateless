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
#else
#include <unistd.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

typedef struct {
  json_t config;
} curl_config_t;

curl_config_t curl_config = {0};

const char* CURL_METHODS[] = {"GET", "POST", "PUT", "DELETE"};

#define DEFAULT_CONFIG "{\"eth_rpc\":[\"https://nameless-sly-reel.quiknode.pro/5937339c28c09a908994b74e2514f0f6cfdac584/\",\"https://eth-mainnet.g.alchemy.com/v2/B8W2IZrDkCkkjKxQOl70XNIy4x4PT20S\",\"https://rpc.ankr.com/eth/33d0414ebb46bda32a461ecdbd201f9cf5141a0acb8f95c718c23935d6febfcd\"]," \
                       "\"beacon_api\":[\"https://lodestar-mainnet.chainsafe.io\"]}"

char* cache_dir = NULL;

void curl_set_cache_dir(const char* dir) {
  cache_dir = strdup(dir);
  MKDIR(cache_dir);
}

#define return_error(req, msg) \
  {                            \
    req->error = strdup(msg);  \
    return;                    \
  }

#ifdef TEST
static char* REQ_TEST_DIR = NULL;
static void  test_write_file(const char* filename, bytes_t data) {
  if (!REQ_TEST_DIR) return;
  buffer_t buf = {0};
  bytes_write(data, fopen(bprintf(&buf, "%s/%s", REQ_TEST_DIR, filename), "w"), true);
  buffer_free(&buf);
}

void curl_set_test_dir(const char* dir) {
  buffer_t buf = {0};
  REQ_TEST_DIR = bprintf(&buf, "%s/%s", TESTDATA_DIR, dir);
  MKDIR(REQ_TEST_DIR);
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

static void configure() {
  char*   config_file = getenv("C4_CONFIG");
  bytes_t content     = {0};
  if (config_file) content = bytes_read(config_file);
  if (!content.data) content = bytes_read("c4_config.json");
  if (content.data) {
    curl_set_config(json_parse((char*) content.data));
    free(content.data);
  }
  else
    curl_set_config(json_parse(DEFAULT_CONFIG));
}

static bool handle(data_request_t* req, char* url, buffer_t* error) {
  if (req->payload.len && req->payload.data)
    log_info("req: %s : %j", url, (json_t) {.start = (char*) req->payload.data, .len = req->payload.len, .type = JSON_TYPE_OBJECT});
  else
    log_info("req: %s", req->url);

  CURL* curl = curl_easy_init();
  if (!curl) {
    buffer_add_chars(error, "Failed to initialize curl");
    return false;
  }
  buffer_t buffer = {0};

  curl_easy_setopt(curl, CURLOPT_URL, url);
  if (req->payload.len && req->payload.data) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->payload.data);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long) req->payload.len);
  }

  struct curl_slist* headers = NULL;
  headers                    = curl_slist_append(headers, req->encoding == C4_DATA_ENCODING_JSON ? "Accept: application/json" : "Accept: application/octet-stream");
  if (req->payload.len && req->payload.data)
    headers = curl_slist_append(headers, req->encoding == C4_DATA_ENCODING_JSON ? "Content-Type: application/json" : "Content-Type: application/octet-stream");
  headers = curl_slist_append(headers, "charsets: utf-8");
  headers = curl_slist_append(headers, "User-Agent: c4 curl ");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_append);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, (uint64_t) 120);
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, CURL_METHODS[req->method]);
  CURLcode res = curl_easy_perform(curl);
  if (res == CURLE_OK) {
    req->response = buffer.data;
  }
  else {
    buffer_free(&buffer);
    buffer_add_chars(error, curl_easy_strerror(res));
  }
  curl_easy_cleanup(curl);
  return res == CURLE_OK;
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

void curl_fetch(data_request_t* req) {
  // make sure there is a config

#ifdef TEST

  if (cache_dir && check_cache(req)) return;

#endif
  if (!curl_config.config.start) configure();

  char*    last_error = NULL;
  buffer_t buffer     = {0};
  buffer_t error      = {0};
  buffer_t url        = {0};
  json_t   servers    = {0};
  switch (req->type) {
    case C4_DATA_TYPE_ETH_RPC:
      servers = json_get(curl_config.config, "eth_rpc");
      break;
    case C4_DATA_TYPE_BEACON_API:
      servers = json_get(curl_config.config, "beacon_api");
      break;
    case C4_DATA_TYPE_REST_API:
      break;
  }

  if (req->type != C4_DATA_TYPE_REST_API && servers.type != JSON_TYPE_ARRAY) return_error(req, "Invalid servers in config");
  int  i       = 0;
  bool success = false;
  if (req->type == C4_DATA_TYPE_REST_API)
    success = handle(req, NULL, &error);
  else
    json_for_each_value(servers, server) {
      if (req->node_exclude_mask & (1 << i)) {
        i++;
        continue;
      }
      req->response_node_index = i;
      error.data.len           = 0;
      url.data.len             = 0;
      buffer_add_chars(&url, json_as_string(server, &buffer));
      if (req->url && *req->url) {
        buffer_add_chars(&url, "/");
        buffer_add_chars(&url, req->url);
      }

      success = handle(req, (char*) url.data.data, &error);
      if (success) break;
      i++;
    }
  if (!success) return_error(req, "All servers failed");

  buffer_free(&buffer);
  if (error.data.len)
    req->error = (char*) error.data.data;
  else if (error.data.data)
    buffer_free(&error);
  buffer_free(&url);

#ifdef TEST
  if (req->response.data && REQ_TEST_DIR) {
    char* test_filename = c4_req_mockname(req);
    test_write_file(test_filename, req->response);
    free(test_filename);
  }
  if (cache_dir && req->response.data) write_cache(req);
#endif
}

void curl_set_config(json_t config) {
  if (curl_config.config.start) free((void*) curl_config.config.start);
  curl_config.config = (json_t) {.len = config.len, .start = strdup(config.start), .type = config.type};
}