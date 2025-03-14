#include "evmone_c_wrapper.h"
#include <string.h>

// Stub implementation of evmc_create_evmone
struct evmc_vm* evmc_create_evmone() {
  return NULL;
}

// Stub implementation of evmone_execute
struct evmc_result evmone_execute(struct evmc_vm*                   vm,
                                  const struct evmc_host_interface* host,
                                  void*                             context,
                                  int                               revision,
                                  const struct evmc_message*        msg,
                                  const uint8_t*                    code,
                                  size_t                            code_size) {
  struct evmc_result result;
  memset(&result, 0, sizeof(result));
  result.status_code = EVMC_FAILURE;
  return result;
}