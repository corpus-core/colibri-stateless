#include "logger.h"
#include "bytes.h"

static log_level_t log_level = LOG_WARN;

void c4_set_log_level(log_level_t level) {
  log_level = level;
}

log_level_t c4_get_log_level() {
  return log_level;
}
