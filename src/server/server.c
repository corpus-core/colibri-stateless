#include "civetweb.h"
#include "handlers.h"
#include "http_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#endif

int main(void) {
  struct mg_callbacks callbacks;
  struct mg_context*  ctx;

  // Initialize the library with SSL support
  unsigned features = mg_init_library(0x2u);
  if ((features & 0x2u) == 0) {
    fprintf(stderr, "Failed to initialize SSL/TLS support\n");
    return 1;
  }
  printf("SSL support initialized successfully\n");

  // Server options
  const char* options[] = {
      "listening_ports", "8080",
      "num_threads", "1", // Single thread event loop mode
      NULL};

  // Initialize callbacks
  memset(&callbacks, 0, sizeof(callbacks));

  // Start the web server
  ctx = mg_start(&callbacks, NULL, options);
  if (!ctx) {
    fprintf(stderr, "Failed to start server\n");
    mg_exit_library();
    return 1;
  }

  // Set up the endpoint handlers
  mg_set_request_handler(ctx, "/api/", lodestar_api_handler, NULL);
  mg_set_request_handler(ctx, "/test", test_api_handler, NULL);
  mg_set_request_handler(ctx, "/statemachine", statemachine_handler, NULL);

  printf("Server started on port 8080\n");
  printf("Try accessing: http://localhost:8080/api/blocks/head\n");
  printf("Or test with:  http://localhost:8080/test\n");
  printf("Try state machine demo: http://localhost:8080/statemachine\n");
  printf("Press Enter to stop the server\n");
  printf("All HTTP handlers use non-blocking callbacks allowing multiple parallel requests\n");

  // Main event loop - process pending requests and check for Enter key
  int running = 1;
  while (running) {
    // Process any pending HTTP client requests - this is where callbacks are invoked
    // when response data becomes available from remote servers
    process_pending_requests();

    // Check if there's input waiting
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval tv;
    tv.tv_sec  = 0;
    tv.tv_usec = 100000; // 100ms timeout

    int select_result = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
    if (select_result > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
      // Input is waiting, read it
      if (getchar() == '\n') {
        running = 0; // Exit loop
      }
    }
  }

  // Stop the server
  mg_stop(ctx);

  // Cleanup library
  mg_exit_library();

  printf("Server stopped\n");
  return 0;
}