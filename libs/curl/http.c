#include "http.h"
#include "request.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
  json_t config;
} curl_config_t;

curl_config_t curl_config = {0};

const char* CURL_METHODS[] = {"GET", "POST", "PUT", "DELETE"};

#define DEFAULT_CONFIG "{\"eth_rpc\":[\"https://rpc.ankr.com/eth\",\"https://eth-mainnet.g.alchemy.com/v2/B8W2IZrDkCkkjKxQOl70XNIy4x4PT20S\"]," \
                       "\"beacon_api\":[\"https://lodestar-mainnet.chainsafe.io\"]}"

#define return_error(req, msg) \
  {                            \
    req->error = strdup(msg);  \
    return;                    \
  }

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

void curl_fetch(data_request_t* req) {
  /*
  char filename[1024];
  sprintf(filename, ".cache/%llx.req", *((long long*) req->id));
  FILE* f = fopen(filename, "rb");
  if (f) {
    unsigned char buffer[1024];
    size_t        bytesRead;
    buffer_t      data = {0};

    while ((bytesRead = fread(buffer, 1, 1024, f)) > 0)
      buffer_append(&data, bytes(buffer, bytesRead));

    fclose(f);
    req->response = data.data;
    return;
  }
*/
  // make sure there is a config
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

  if (req->type == C4_DATA_TYPE_REST_API)
    handle(req, NULL, &error);
  else
    json_for_each_value(servers, server) {
      error.data.len = 0;
      url.data.len   = 0;
      buffer_add_chars(&url, json_as_string(server, &buffer));
      if (req->url && *req->url) {
        buffer_add_chars(&url, "/");
        buffer_add_chars(&url, req->url);
      }

      bool success = handle(req, (char*) url.data.data, &error);
      if (success) break;
    }

  buffer_free(&buffer);
  if (error.data.len)
    req->error = (char*) error.data.data;
  else if (error.data.data)
    buffer_free(&error);
  buffer_free(&url);

  //  if (req->response.data) bytes_write(req->response, fopen(filename, "wb"), true);
}

void curl_set_config(json_t config) {
  if (curl_config.config.start) free((void*) curl_config.config.start);
  curl_config.config = (json_t) {.len = config.len, .start = strdup(config.start), .type = config.type};
}