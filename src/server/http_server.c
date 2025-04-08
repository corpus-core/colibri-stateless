#include "llhttp.h"
#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

static http_handler* handlers = NULL;
static int           handlers_count;

// Function prototypes
static void  on_close(uv_handle_t* handle);
static void  on_write_complete(uv_write_t* req, int status);
static int   on_url(llhttp_t* parser, const char* at, size_t length);
static int   on_method(llhttp_t* parser, const char* at, size_t length);
static int   on_header_field(llhttp_t* parser, const char* at, size_t length);
static int   on_header_value(llhttp_t* parser, const char* at, size_t length);
static int   on_body(llhttp_t* parser, const char* at, size_t length);
static int   on_message_complete(llhttp_t* parser);
static void  alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
static void  on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
static char* status_text(int status);

void c4_register_http_handler(http_handler handler) {
  handlers                   = (http_handler*) safe_realloc(handlers, (handlers_count + 1) * sizeof(http_handler));
  handlers[handlers_count++] = handler;
}

static void on_close(uv_handle_t* handle) {
  client_t* client = (client_t*) handle->data;
  if (!client) return;

  safe_free(client->request.path);
  safe_free(client->request.content_type);
  safe_free(client->request.accept);
  safe_free(client->request.payload);
  safe_free(client);
}

static int on_url(llhttp_t* parser, const char* at, size_t length) {
  client_t* client     = (client_t*) parser->data;
  client->request.path = strndup(at, length);
  return 0;
}

static int on_method(llhttp_t* parser, const char* at, size_t length) {
  client_t* client = (client_t*) parser->data;
  if (strncasecmp(at, "GET", length) == 0)
    client->request.method = C4_DATA_METHOD_GET;
  else if (strncasecmp(at, "POST", length) == 0)
    client->request.method = C4_DATA_METHOD_POST;
  else if (strncasecmp(at, "PUT", length) == 0)
    client->request.method = C4_DATA_METHOD_PUT;
  else if (strncasecmp(at, "DELETE", length) == 0)
    client->request.method = C4_DATA_METHOD_DELETE;
  else
    return HPE_INVALID_METHOD;

  return 0;
}

static int on_header_field(llhttp_t* parser, const char* at, size_t length) {
  client_t* client = (client_t*) parser->data;
  if (length > sizeof(client->current_header) - 1) return HPE_INVALID_HEADER_TOKEN;
  strncpy(client->current_header, at, length);
  client->current_header[length] = '\0';
  return 0;
}

static int on_header_value(llhttp_t* parser, const char* at, size_t length) {
  client_t* client = (client_t*) parser->data;
  if (strcasecmp(client->current_header, "Content-Type") == 0)
    client->request.content_type = strndup(at, length);
  else if (strcasecmp(client->current_header, "Accept") == 0)
    client->request.accept = strndup(at, length);
  return 0;
}

static int on_body(llhttp_t* parser, const char* at, size_t length) {
  client_t* client        = (client_t*) parser->data;
  client->request.payload = (uint8_t*) safe_malloc(length);
  memcpy(client->request.payload, at, length);
  client->request.payload_len = length;
  return 0;
}

static char* method_str(data_request_method_t method) {
  switch (method) {
    case C4_DATA_METHOD_GET:
      return "GET";
    case C4_DATA_METHOD_POST:
      return "POST";
    case C4_DATA_METHOD_PUT:
      return "PUT";
    case C4_DATA_METHOD_DELETE:
      return "DELETE";
    default:
      return "UNKNOWN";
  }
}

static int on_message_complete(llhttp_t* parser) {
  client_t* client = (client_t*) parser->data;
  char*     pl     = client->request.payload_len ? bprintf(NULL, "%J", (json_t) {.type = JSON_TYPE_OBJECT, .start = (char*) client->request.payload, .len = client->request.payload_len}) : NULL;
  fprintf(stderr, "[%s] %s %s\n", method_str(client->request.method), client->request.path, pl ? pl : "");
  if (pl) safe_free(pl);
  for (int i = 0; i < handlers_count; i++) {
    if (handlers[i](client)) return 0;
  }
  c4_http_respond(client, 405, "text/plain", bytes("Method not allowed", 19));
  return 0;
}

static void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  if (suggested_size > 4096) suggested_size = 4096; // we don't expect more than 4096 bytes
  buf->base = (char*) safe_malloc(suggested_size);
  buf->len  = suggested_size;
}

static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  client_t* client = (client_t*) stream->data;
  if (nread > 0) {
    llhttp_errno_t err = llhttp_execute(&(client->parser), buf->base, nread);
    if (err != HPE_OK) {
      const char* reason = llhttp_get_error_reason(&(client->parser));
      fprintf(stderr, "llhttp error: %s\n", reason);
      c4_http_respond(client, 400, "text/plain", bytes(reason, strlen(reason)));
    }
  }
  else if (nread < 0) {
    const char* reason = uv_strerror(nread);
    fprintf(stderr, "uv_read error: %s\n", reason);
    c4_http_respond(client, 500, "text/plain", bytes(reason, strlen(reason)));
  }
  safe_free(buf->base);
}

static char* status_text(int status) {
  switch (status) {
    case 200:
      return "OK";
    case 404:
      return "Not Found";
    case 400:
      return "Bad Request";
    case 401:
      return "Unauthorized";
    case 403:
      return "Forbidden";
    default:
      return "Internal Server Error";
  }
}

void c4_http_respond(client_t* client, int status, char* content_type, bytes_t body) {
  if (!client) {
    fprintf(stderr, "ERROR: Attempted to respond to NULL client\n");
    return;
  }

  if (client->being_closed) {
    fprintf(stderr, "ERROR: Attempted to respond to a client that is already being closed\n");
    return;
  }

  // Mark as being closed to prevent multiple attempts
  client->being_closed = true;

  if (!uv_is_active((uv_handle_t*) &client->handle)) {
    fprintf(stderr, "ERROR: Attempted to write to inactive client handle - closing directly\n");

    // Instead of trying to close directly, simply return as the client is already marked as closing
    return;
  }

  char     tmp[500];
  uv_buf_t uvbuf[] = {
      {.base = tmp, .len = 0},
      {.base = (char*) body.data, .len = body.len}};

  uvbuf[0].len = snprintf(tmp, sizeof(tmp), "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n",
                          status, status_text(status), content_type, body.len);

  // Set up a write callback to safely close the handle after writing
  uv_write_t* write_req = &client->write_req;

  int result = uv_write(write_req, (uv_stream_t*) &client->handle, uvbuf, 2, on_write_complete);

  if (result < 0) {
    fprintf(stderr, "ERROR: Failed to write HTTP response: %s\n", uv_strerror(result));
    // If write fails, close the handle now
    uv_close((uv_handle_t*) &client->handle, on_close);
  }
  // If write succeeds, the handle will be closed in the on_write_complete callback
}

// Callback for when a write completes - close the handle safely
static void on_write_complete(uv_write_t* req, int status) {
  client_t* client = (client_t*) req->handle->data;

  if (status < 0) {
    fprintf(stderr, "ERROR: Write completed with error: %s\n", uv_strerror(status));
  }

  // Close the handle now that writing is done
  uv_close((uv_handle_t*) &client->handle, on_close);
}

void c4_on_new_connection(uv_stream_t* server, int status) {
  if (status < 0) {
    fprintf(stderr, "New connection error %s\n", uv_strerror(status));
    return;
  }
  uv_loop_t* loop   = server->loop;
  client_t*  client = (client_t*) safe_calloc(1, sizeof(client_t));
  uv_tcp_init(loop, &client->handle);
  client->handle.data  = client;
  client->being_closed = false;

  llhttp_settings_init(&client->settings);
  client->settings.on_url              = on_url;
  client->settings.on_method           = on_method;
  client->settings.on_header_field     = on_header_field;
  client->settings.on_header_value     = on_header_value;
  client->settings.on_body             = on_body;
  client->settings.on_message_complete = on_message_complete;

  llhttp_init(&client->parser, HTTP_REQUEST, &client->settings);
  client->parser.data = client;

  int err = uv_accept(server, (uv_stream_t*) &client->handle);
  if (err == 0)
    err = uv_read_start((uv_stream_t*) &client->handle, alloc_buffer, on_read);
  if (err < 0) {
    const char* reason = uv_strerror(err);
    fprintf(stderr, "uv_accept error %s\n", reason);
    c4_http_respond(client, 500, "text/plain", bytes(reason, strlen(reason)));
  }
}
