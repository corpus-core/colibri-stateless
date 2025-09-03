#include "handler.h"
#include "util/logger.h"

// Forward declarations for handlers from the moved files
// These will be registered with the main server.

void op_server_init(http_server_t* server) {
  OP_HANDLER_CHECK(server);

  log_info("Initializing OP-Stack server handlers...");
}

void op_server_shutdown(http_server_t* server) {
  OP_HANDLER_CHECK(server);
}
