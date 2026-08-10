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

#include "logger.h"
#include "bytes.h"
#include "state.h"

static log_level_t       log_level    = LOG_WARN;
static c4_stacksize_fn_t stacksize_fn = NULL;

char* c4_req_info_short(data_request_type_t type, char* path, bytes_t payload) {
  static uint8_t req_info_buf[1024];
  buffer_t       buf = stack_buffer(req_info_buf);
  if (type == C4_DATA_TYPE_INTERN)
    bprintf(&buf, " %s ", path ? path : "");
  else
    bprintf(&buf, "(%s)  %s", type == C4_DATA_TYPE_BEACON_API ? "beacon" : "rpc", path ? path : "");
  if (payload.len) {
    json_t method = json_get(json_parse((char*) payload.data), "method");
    if (method.type == JSON_TYPE_STRING)
      bprintf(&buf, " %j", method);
  }
  return (char*) req_info_buf;
}

char* c4_req_info(data_request_type_t type, char* path, bytes_t payload) {
  static uint8_t req_info_buf[1024];
  buffer_t       buf = stack_buffer(req_info_buf);
  if (type == C4_DATA_TYPE_INTERN)
    bprintf(&buf, BRIGHT_GREEN(" %s "), path ? path : "");
  else
    bprintf(&buf, CYAN("(%s)" BRIGHT_GREEN(" %s")), type == C4_DATA_TYPE_BEACON_API ? "beacon" : "rpc", path ? path : "");
  if (payload.len) {
    json_t json   = json_parse((char*) payload.data);
    json_t method = json_get(json, "method");
    json_t params = json_get(json, "params");
    json_t c4     = json_get(json, "c4");
    if (method.type == JSON_TYPE_STRING && params.type == JSON_TYPE_ARRAY) {
      params.start++;
      params.len -= 2;
      bprintf(&buf, BOLD("%j") GRAY(" (%j)"), method, params);
      if (c4.type == JSON_TYPE_STRING && c4.len > 2)
        bprintf(&buf, " c4: " YELLOW("%j"), c4);
    }
    else
      bprintf(&buf, GRAY("%r"), payload);
  }
  return (char*) req_info_buf;
}

void c4_set_log_level(log_level_t level) {
  log_level = level;
}

log_level_t c4_get_log_level() {
  return log_level;
}

void c4_set_stacksize_fn(c4_stacksize_fn_t fn) {
  stacksize_fn = fn;
}

c4_stacksize_fn_t c4_get_stacksize_fn(void) {
  return stacksize_fn;
}
