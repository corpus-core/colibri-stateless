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
#define log_error(fmt, ...) \
  if (c4_get_log_level() >= 1) _log("\033[31mERROR", fmt, ##__VA_ARGS__)
#define log_info(fmt, ...) \
  if (c4_get_log_level() >= 2) _log("INFO", fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...) \
  if (c4_get_log_level() >= 3) _log("\033[33mWARN", fmt, ##__VA_ARGS__)
#define log_debug(fmt, ...) \
  if (c4_get_log_level() >= 4) _log("\033[33mDEBUG", fmt, ##__VA_ARGS__)
#define log_debug_full(fmt, ...) \
  if (c4_get_log_level() >= 5) _log("\033[33mDEBUG", fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif