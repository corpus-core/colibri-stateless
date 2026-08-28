#include "handler.h"
#include "../verifier/op_chains_conf.h"
#include "logger.h"
#include "op_conf.h"
#include "util/bytes.h"
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

// Timer für verzögerte Initialisierung
static uv_timer_t delayed_init_timer;

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
