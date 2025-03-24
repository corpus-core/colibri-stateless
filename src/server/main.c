#include "server.h"
#include <curl/curl.h>
#include <errno.h>
#include <llhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <uv.h>

// Macro for simplified libuv error handling
#define UV_CHECK(op, expr)                                           \
  do {                                                               \
    int r = (expr);                                                  \
    if (r != 0) {                                                    \
      if (r == UV_EADDRINUSE && strcmp(op, "TCP listening") == 0)    \
        fprintf(stderr, "Error: Port %d is already in use\n", port); \
      else                                                           \
        fprintf(stderr, "Error: %s failed: %s (%s)\n",               \
                (op),                                                \
                uv_strerror(r),                                      \
                uv_err_name(r));                                     \
      return 1;                                                      \
    }                                                                \
  } while (0)

int main() {
  uv_timer_t         timer;
  uv_tcp_t           server;
  struct sockaddr_in addr;
  int                port = 8080;
  uv_loop_t*         loop = uv_default_loop();

  // register http-handler
  c4_register_http_handler(c4_handle_proof_request);
  c4_register_http_handler(c4_proxy);
  c4_register_http_handler(c4_handle_status);

  if (!loop) {
    fprintf(stderr, "Error: Failed to initialize default uv loop\n");
    return 1;
  }

  UV_CHECK("TCP initialization", uv_tcp_init(loop, &server));
  UV_CHECK("IP address parsing", uv_ip4_addr("0.0.0.0", port, &addr));
  UV_CHECK("TCP binding", uv_tcp_bind(&server, (const struct sockaddr*) &addr, 0));
  UV_CHECK("TCP listening", uv_listen((uv_stream_t*) &server, 128, c4_on_new_connection));
  UV_CHECK("Timer initialization", uv_timer_init(loop, &timer));

  printf("C4 Server running on port %d\n", port);

  // Initialize curl right before starting the event loop
  c4_init_curl(&timer);

  UV_CHECK("Event loop", uv_run(loop, UV_RUN_DEFAULT));

  c4_cleanup_curl();
  return 0;
}