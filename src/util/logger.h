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

#ifndef C4_LOGGER_H
#define C4_LOGGER_H

#include "state.h"

#define RED(txt)            "\x1b[31m" txt "\x1b[0m"
#define GREEN(txt)          "\x1b[32m" txt "\x1b[0m"
#define YELLOW(txt)         "\x1b[33m" txt "\x1b[0m"
#define BLUE(txt)           "\x1b[34m" txt "\x1b[0m"
#define MAGENTA(txt)        "\x1b[35m" txt "\x1b[0m"
#define CYAN(txt)           "\x1b[36m" txt "\x1b[0m"
#define GRAY(txt)           "\x1b[90m" txt "\x1b[0m"
#define BOLD(txt)           "\x1b[1m" txt "\x1b[0m"
#define UNDERLINE(txt)      "\x1b[4m" txt "\x1b[0m"
#define BRIGHT_RED(txt)     "\x1b[91m" txt "\x1b[0m"
#define BRIGHT_GREEN(txt)   "\x1b[92m" txt "\x1b[0m"
#define BRIGHT_YELLOW(txt)  "\x1b[93m" txt "\x1b[0m"
#define BRIGHT_BLUE(txt)    "\x1b[94m" txt "\x1b[0m"
#define BRIGHT_MAGENTA(txt) "\x1b[95m" txt "\x1b[0m"
#define BRIGHT_CYAN(txt)    "\x1b[96m" txt "\x1b[0m"

#ifdef __cplusplus
extern "C" {
#endif

#include "bytes.h"
typedef enum {
  LOG_SILENT     = 0,
  LOG_ERROR      = 1,
  LOG_INFO       = 2,
  LOG_WARN       = 3,
  LOG_DEBUG      = 4,
  LOG_DEBUG_FULL = 5
} log_level_t;

void        c4_set_log_level(log_level_t level);
log_level_t c4_get_log_level();

#define _log_with_line(prefix, fmt, ...)                    \
  {                                                         \
    buffer_t log_buf = {0};                                 \
    bprintf(&log_buf, "%s\033[0m" GRAY(" %s:%d "),          \
            prefix, __func__, __LINE__);                    \
    bprintf(&log_buf, fmt, ##__VA_ARGS__);                  \
    buffer_add_chars(&log_buf, "\n");                       \
    fwrite(log_buf.data.data, 1, log_buf.data.len, stderr); \
    buffer_free(&log_buf);                                  \
  }
#define _log(prefix, fmt, ...)                              \
  {                                                         \
    buffer_t log_buf = {0};                                 \
    bprintf(&log_buf, "%s\033[0m ", prefix);                \
    bprintf(&log_buf, fmt, ##__VA_ARGS__);                  \
    buffer_add_chars(&log_buf, "\n");                       \
    fwrite(log_buf.data.data, 1, log_buf.data.len, stderr); \
    buffer_free(&log_buf);                                  \
  }
#define log_error(fmt, ...)                                                          \
  do {                                                                               \
    if (c4_get_log_level() >= 1) _log_with_line("\033[31mERROR", fmt, ##__VA_ARGS__) \
  } while (0)
#define log_info(fmt, ...)                                                 \
  do {                                                                     \
    if (c4_get_log_level() >= 2) _log("\033[90mINFO ", fmt, ##__VA_ARGS__) \
  } while (0)
#define log_warn(fmt, ...)                                                 \
  do {                                                                     \
    if (c4_get_log_level() >= 3) _log("\033[33mWARN ", fmt, ##__VA_ARGS__) \
  } while (0)
#define log_debug(fmt, ...)                                                          \
  do {                                                                               \
    if (c4_get_log_level() >= 4) _log_with_line("\033[33mDEBUG", fmt, ##__VA_ARGS__) \
  } while (0)
#define log_debug_full(fmt, ...)                                                     \
  do {                                                                               \
    if (c4_get_log_level() >= 5) _log_with_line("\033[33mDEBUG", fmt, ##__VA_ARGS__) \
  } while (0)
char* c4_req_info(data_request_type_t type, char* path, bytes_t payload);
char* c4_req_info_short(data_request_type_t type, char* path, bytes_t payload);

#ifdef __cplusplus
}
#endif

#endif