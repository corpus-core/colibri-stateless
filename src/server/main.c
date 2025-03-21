#include "server.h"
#include <curl/curl.h>
#include <llhttp.h>
#include <stdlib.h>
#include <uv.h>

int main() {

  c4_register_http_handler(c4_handle_proof_request);
  c4_register_http_handler(c4_proxy);
  c4_register_http_handler(c4_handle_status);

  uv_loop_t* loop = uv_default_loop();
  uv_timer_t timer;
  uv_timer_init(loop, &timer);
  c4_init_curl(&timer);

  uv_tcp_t server;
  uv_tcp_init(loop, &server);
  struct sockaddr_in addr;
  uv_ip4_addr("0.0.0.0", 8080, &addr);
  uv_tcp_bind(&server, (const struct sockaddr*) &addr, 0);
  uv_listen((uv_stream_t*) &server, 128, c4_on_new_connection);

  uv_run(loop, UV_RUN_DEFAULT);
  c4_cleanup_curl();
  return 0;
}