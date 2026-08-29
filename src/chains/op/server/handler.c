#include "handler.h"
#include "logger.h"

void op_server_init(http_server_t* server) {
  OP_HANDLER_CHECK(server);

  log_info("Initializing OP-Stack server handlers...");
  c4_register_internal_handler(c4_handle_preconf);
}

void op_server_shutdown(http_server_t* server) {
  OP_HANDLER_CHECK(server);

  log_info("🛑 Shutting down OP server handler...");
  log_info("✅ OP server handler shutdown complete");
}
