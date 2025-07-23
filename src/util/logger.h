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

#define _log(prefix, fmt, ...)                                  \
  {                                                             \
    buffer_t buf = {0};                                         \
    bprintf(&buf, fmt, ##__VA_ARGS__);                          \
    fprintf(stderr, "%s\033[0m\033[32m %s:%d\033[0m %s\n",      \
            prefix, __func__, __LINE__, (char*) buf.data.data); \
    buffer_free(&buf);                                          \
  }
#define log_error(fmt, ...)                                                \
  do {                                                                     \
    if (c4_get_log_level() >= 1) _log("\033[31mERROR", fmt, ##__VA_ARGS__) \
  } while (0)
#define log_info(fmt, ...)                                        \
  do {                                                            \
    if (c4_get_log_level() >= 2) _log("INFO", fmt, ##__VA_ARGS__) \
  } while (0)
#define log_warn(fmt, ...)                                                \
  do {                                                                    \
    if (c4_get_log_level() >= 3) _log("\033[33mWARN", fmt, ##__VA_ARGS__) \
  } while (0)
#define log_debug(fmt, ...)                                                \
  do {                                                                     \
    if (c4_get_log_level() >= 4) _log("\033[33mDEBUG", fmt, ##__VA_ARGS__) \
  } while (0)
#define log_debug_full(fmt, ...)                                           \
  do {                                                                     \
    if (c4_get_log_level() >= 5) _log("\033[33mDEBUG", fmt, ##__VA_ARGS__) \
  } while (0)

#ifdef __cplusplus
}
#endif

#endif