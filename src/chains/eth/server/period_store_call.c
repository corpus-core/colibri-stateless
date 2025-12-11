/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "period_store_call.h"
#include "eth_conf.h"
#include "logger.h"
#include "period_store_internal.h"
#include "server.h"
#include "ssz.h"
#include "uv_util.h"

#include <stdlib.h>
#include <string.h>

static const char* internal_path = "period_store/";
static void        c4_handle_period_master_write_cb(void* user_data, file_data_t* files, int num_files) {
  if (files[0].error)
    log_error("period_store: could not write period master: %s", files[0].error);
  c4_file_data_array_free(files, num_files, 0);
}

static void c4_handle_period_master_cb(client_t* client, void* user_data, data_request_t* data) {
  single_request_t* r = (single_request_t*) user_data;
  if (data->error) {
    log_error("period_store: could not read period master: %s", data->error);

    // transfer ownership to the response
    r->req->error = data->error;
    data->error   = NULL;
  }
  else {
    // write the data to the period store
    file_data_t f = {.data = bytes_dup(data->response), .limit = data->response.len, .offset = 0, .path = bprintf(NULL, "%s/%s", eth_config.period_store, r->req->url + strlen(internal_path))};
    c4_write_files_uv(NULL, c4_handle_period_master_write_cb, &f, 1, O_WRONLY | O_CREAT | O_TRUNC, 0666);

    // transfer ownership to the response
    r->req->response = data->response;
    data->response   = NULL_BYTES;
  }

  safe_free(data->url);
  safe_free(data);
  c4_internal_call_finish(r);
}

static void c4_handle_period_store_cb(void* user_data, file_data_t* files, int num_files) {
  single_request_t* r = (single_request_t*) user_data;

  // missing, so we try to fetch it from the master node
  if ((files[0].error && strstr(files[0].error, "such file or directory") != NULL) && eth_config.period_master_url) {
    char* path = files[0].path + strlen(eth_config.period_store);
    if (*path == '/') path++;
    data_request_t* req = (data_request_t*) safe_calloc(1, sizeof(data_request_t));
    req->url            = bprintf(NULL, "%s%s%s", eth_config.period_master_url, eth_config.period_master_url[strlen(eth_config.period_master_url) - 1] == '/' ? "" : "/", path);
    req->method         = C4_DATA_METHOD_GET;
    req->chain_id       = http_server.chain_id;
    req->type           = C4_DATA_TYPE_REST_API;
    req->encoding       = C4_DATA_ENCODING_SSZ;
    c4_file_data_array_free(files, num_files, 0);
    c4_add_request(r->parent->client, req, r, c4_handle_period_master_cb);
    return;
  }

  else if (files[0].error) {
    log_error("period_store: could not read period store: %s", files[0].error);
    r->req->error = strdup(files[0].error);
  }
  else {
    // transfer ownership of the data to the response
    r->req->response   = files[0].data;
    files[0].data.data = NULL;
  }
  c4_file_data_array_free(files, num_files, 0);
  c4_internal_call_finish(r);
}

bool c4_handle_period_store(single_request_t* r) {
  if (strncmp(r->req->url, internal_path, strlen(internal_path))) return false;

  // make sure the period-store is configured
  if (!eth_config.period_store) {
    r->req->error = strdup("period_store not configured");
    c4_internal_call_finish(r);
    return true;
  }

  // we simply read the file from the period-store
  file_data_t f = {.path = bprintf(NULL, "%s/%s", eth_config.period_store, r->req->url + strlen(internal_path))};
  c4_read_files_uv(r, c4_handle_period_store_cb, &f, 1);

  return true;
}
