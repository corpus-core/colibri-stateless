#include "handler.h"
#include "uv_util.h"

static void c4_handle_preconf_cb(void* user_data, file_data_t* files, int num_files) {
  single_request_t* r = (single_request_t*) user_data;
  if (files[0].error)
    r->req->error = strdup(files[0].error);
  else {
    r->req->response   = files[0].data;
    files[0].data.data = NULL;
  }
  c4_file_data_array_free(files, num_files, 0);
  c4_internal_call_finish(r);
}

bool c4_handle_preconf(single_request_t* r) {
  const char* path = "preconf/";
  if (strncmp(r->req->url, path, strlen(path))) return false;
  if (!http_server.preconf_storage_dir) {
    r->req->error = strdup("preconf_storage_dir not configured!");
    c4_internal_call_finish(r);
    return true;
  }

  char* block_identifier = r->req->url + strlen(path);
  char* file_name        = NULL;
  if (strcmp(block_identifier, "latest") == 0 || strcmp(block_identifier, "pre_latest") == 0)
    file_name = bprintf(NULL, "%s/%s.raw", http_server.preconf_storage_dir, block_identifier);
  else if (strncmp(block_identifier, "0x", 2) == 0 || strncmp(block_identifier, "0X", 2) == 0)
    file_name = bprintf(NULL, "%s/block_%l_%l.raw", http_server.preconf_storage_dir, r->req->chain_id, strtoull(block_identifier + 2, NULL, 16));
  else {
    r->req->error = bprintf(NULL, "Invalid block identifier: %s", block_identifier);
    c4_internal_call_finish(r);
    return true;
  }

  file_data_t f = {.path = file_name};
  c4_read_files_uv(r, c4_handle_preconf_cb, &f, 1);
  return true;
}