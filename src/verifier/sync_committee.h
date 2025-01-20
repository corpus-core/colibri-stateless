#include "../util/bytes.h"
#include <stdint.h>

typedef struct {
  uint64_t last_period;
  uint64_t current_period;
  bytes_t  validators;
} c4_sync_state_t;

const c4_sync_state_t c4_get_validators(uint64_t period);
