/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

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

// Forward declaration
static void on_close(uv_handle_t* handle);

// Centralized function to close a client connection safely
static void close_client_connection(client_t* client) {
  if (client == NULL) {
    return;
  }

  // Check if uv_close has already been called on this handle
  if (uv_is_closing((uv_handle_t*) &client->handle)) {
    // If it's already closing but our flag isn't set, set it for consistency.
    if (!client->being_closed) client->being_closed = true;
    return;
  }

  // Mark that we are initiating the close sequence now from application logic
  client->being_closed = true;

  // Optional: Log if we are closing an inactive handle, though uv_close handles it gracefully.
  // if (!uv_is_active((uv_handle_t*)&client->handle)) {
  //   fprintf(stderr, "Debug: Closing an inactive client handle %p via close_client_connection\n", (void*)client);
  // }

  uv_close((uv_handle_t*) &client->handle, on_close);
}

static void reset_client_request_data(client_t* client) {
  safe_free(client->request.path);
  client->request.path = NULL;
  safe_free(client->request.content_type);
  client->request.content_type = NULL;
  safe_free(client->request.accept);
  client->request.accept = NULL;
  safe_free(client->request.payload);
  client->request.payload     = NULL;
  client->request.payload_len = 0;
  // client->request.method is an enum, gets overwritten by parser.
  memset(client->current_header, 0, sizeof(client->current_header));
  client->message_complete_reached = false; // Reset for keep-alive
}

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
#ifdef HTTP_SERVER_GEO
  safe_free(client->request.geo_city);
  safe_free(client->request.geo_country);
  safe_free(client->request.geo_latitude);
  safe_free(client->request.geo_longitude);
#endif
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
#ifdef HTTP_SERVER_GEO
  else if (strcasecmp(client->current_header, "Country-Code") == 0)
    client->request.geo_country = strndup(at, length);
  else if (strcasecmp(client->current_header, "City-Name") == 0)
    client->request.geo_city = strndup(at, length);
  else if (strcasecmp(client->current_header, "Latitude") == 0)
    client->request.geo_latitude = strndup(at, length);
  else if (strcasecmp(client->current_header, "Longitude") == 0)
    client->request.geo_longitude = strndup(at, length);
#endif
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

static void log_request(client_t* client) {
  if (strcmp(client->request.path, "/health") == 0) return;      // no healthcheck logging
  if (strcmp(client->request.path, "/healthcheck") == 0) return; // no healthcheck logging
  if (strcmp(client->request.path, "/metrics") == 0) return;     // no metrics logging
  char* pl = client->request.payload_len ? bprintf(NULL, "%J", (json_t) {.type = JSON_TYPE_OBJECT, .start = (char*) client->request.payload, .len = client->request.payload_len}) : NULL;
  fprintf(stderr,
#ifdef HTTP_SERVER_GEO
          "[%s] %s %s (%s/%s)\n",
#else
          "[%s] %s %s\n",
#endif
          method_str(client->request.method),
          client->request.path, pl ? pl : ""
#ifdef HTTP_SERVER_GEO
          ,
          client->request.geo_city ? client->request.geo_city : "",
          client->request.geo_country ? client->request.geo_country : ""
#endif
  );
  if (pl) safe_free(pl);
}

#ifdef HTTP_SERVER_GEO

static void c4_metrics_prune_geo_locations() {
  const uint64_t GEO_EXPIRY_MS = 24 * 60 * 60 * 1000; // 24 hours
  uint64_t       now           = current_ms();
  size_t         new_count     = 0;
  for (size_t i = 0; i < http_server.stats.geo_locations_count; i++) {
    if (now - http_server.stats.geo_locations[i].last_access > GEO_EXPIRY_MS) {
      // clean up the old location
      safe_free(http_server.stats.geo_locations[i].city);
      safe_free(http_server.stats.geo_locations[i].country);
      safe_free(http_server.stats.geo_locations[i].latitude);
      safe_free(http_server.stats.geo_locations[i].longitude);
    }
    else if (new_count != i)
      http_server.stats.geo_locations[new_count++] = http_server.stats.geo_locations[i];
  }
  http_server.stats.geo_locations_count = new_count;
}

static void c4_metrics_update_geo(client_t* client) {
  // Do not track metrics for internal endpoints
  if (strcmp(client->request.path, "/health") == 0) return;
  if (strcmp(client->request.path, "/healthcheck") == 0) return;
  if (strcmp(client->request.path, "/metrics") == 0) return;
  if (client->request.method != C4_DATA_METHOD_POST) return;
  if (!client->request.geo_city || !client->request.geo_country) return;

  const size_t MAX_GEO_LOCATIONS = 1000; // TODO: make this configurable
  uint64_t     now               = current_ms();

  for (size_t i = 0; i < http_server.stats.geo_locations_count; i++) {
    if (strcmp(http_server.stats.geo_locations[i].city, client->request.geo_city) == 0 && strcmp(http_server.stats.geo_locations[i].country, client->request.geo_country) == 0) {
      geo_location_t* loc = &http_server.stats.geo_locations[i];
      loc->count++;
      loc->last_access = now;
      return;
    }
  }

  // nothing found, so we need to add it
  // but first, clean up the old locations
  if (http_server.stats.geo_locations_count >= http_server.stats.geo_locations_capacity)
    c4_metrics_prune_geo_locations();

  if (http_server.stats.geo_locations_count >= http_server.stats.geo_locations_capacity) {
    size_t new_capacity = http_server.stats.geo_locations_capacity == 0 ? 16 : http_server.stats.geo_locations_capacity * 2;
    if (new_capacity > MAX_GEO_LOCATIONS) new_capacity = MAX_GEO_LOCATIONS;

    if (http_server.stats.geo_locations_count >= new_capacity) {
      fprintf(stderr, "WARN: Geo location list is full. Dropping new location: %s, %s\n", client->request.geo_city, client->request.geo_country);
      return;
    }

    http_server.stats.geo_locations          = safe_realloc(http_server.stats.geo_locations, new_capacity * sizeof(geo_location_t));
    http_server.stats.geo_locations_capacity = new_capacity;
  }

  geo_location_t* loc = &http_server.stats.geo_locations[http_server.stats.geo_locations_count++];
  loc->city           = strdup(client->request.geo_city);
  loc->country        = strdup(client->request.geo_country);
  loc->latitude       = client->request.geo_latitude ? strdup(client->request.geo_latitude) : NULL;
  loc->longitude      = client->request.geo_longitude ? strdup(client->request.geo_longitude) : NULL;
  loc->count          = 1;
  loc->last_access    = now;
}
#endif

static int on_message_complete(llhttp_t* parser) {
  client_t* client                 = (client_t*) parser->data;
  client->message_complete_reached = true; // Mark that message is complete
  log_request(client);
#ifdef HTTP_SERVER_GEO
  c4_metrics_update_geo(client);
#endif
  http_server.stats.open_requests++;
  http_server.stats.last_request_time = current_ms();
  http_server.stats.total_requests++;

  // During graceful shutdown, reject all new requests with 503 Service Unavailable
  if (graceful_shutdown_in_progress) {
    c4_write_error_response(client, 503, "Server is shutting down, please try another server");
    return 0;
  }

  for (int i = 0; i < handlers_count; i++) {
    if (handlers[i](client)) return 0;
  }
  c4_write_error_response(client, 405, "Method not allowed");
  return 0;
}

static void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  if (suggested_size > 4096) suggested_size = 4096; // we don't expect more than 4096 bytes
  buf->base = (char*) safe_malloc(suggested_size);
  buf->len  = suggested_size;
}

static void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  client_t*   client                 = (client_t*) stream->data;
  bool        should_call_responder  = false; // Renamed from should_close_client to be more specific
  bool        immediate_close_needed = false; // If true, we close without trying to respond further
  int         error_status_code      = 0;
  const char* error_reason           = NULL;
  char        temp_reason_buffer[256]; // For constructing error messages if needed

  if (nread > 0) {
    client->keep_alive_idle = false; // Activity detected, not idle anymore
    llhttp_errno_t err      = llhttp_execute(&(client->parser), buf->base, nread);
    if (err != HPE_OK) {
      error_reason = llhttp_get_error_reason(&(client->parser));
      if (error_reason == NULL || strlen(error_reason) == 0) {
        // Provide a more generic error if llhttp_get_error_reason is unhelpful
        snprintf(temp_reason_buffer, sizeof(temp_reason_buffer), "HTTP parsing error: %s", llhttp_errno_name(err));
        error_reason = temp_reason_buffer;
      }
      fprintf(stderr, "llhttp error: %s (code: %d) on client %p\n", error_reason, err, (void*) client);
      error_status_code      = 400; // Bad Request
      should_call_responder  = true;
      immediate_close_needed = true;
    }
  }
  else if (nread == 0 || nread == UV_EOF) { // Graceful EOF or libuv's EOF signal (-4095)
    if (client->keep_alive_idle) {
      // Client closed an idle keep-alive connection. This is normal.
      //      fprintf(stderr, "DEBUG: Client %p closed idle keep-alive connection (code: %zd).\n", (void*) client, nread);
      immediate_close_needed = true;
      // should_call_responder remains false, no error response needed.
    }
    else {
      // EOF occurred unexpectedly (e.g., mid-request or before parser was reset for keep-alive idle state)
      // Or, if message_complete_reached is false, it implies an incomplete request.
      if (!client->message_complete_reached) { // Check if a message was even completed in this cycle
        snprintf(temp_reason_buffer, sizeof(temp_reason_buffer), "Incomplete request: client connection ended prematurely (code: %zd)", nread);
        error_reason = temp_reason_buffer;
        fprintf(stderr, "WARN: %s on client %p\n", error_reason, (void*) client);
        error_status_code     = 400;  // Bad Request
        should_call_responder = true; // Try to send a 400 error
      }
      else {
        // message_complete_reached is true, but keep_alive_idle was false.
        // This could happen if EOF arrives right after on_message_complete but before on_write_complete could set keep_alive_idle.
        // Or client closed a non-keep-alive connection after response was sent.
        fprintf(stderr, "INFO: Client %p connection ended (code: %zd) after message completion processing.\n", (void*) client, nread);
        // No error response needed if message was already handled.
      }
      immediate_close_needed = true;
    }
  }
  else { // nread < 0 and not UV_EOF (other libuv errors)
    error_reason = uv_strerror(nread);
    fprintf(stderr, "uv_read error: %s (code: %zd) on client %p\n", error_reason, nread, (void*) client);
    error_status_code      = 500; // Internal Server Error (could be 400 for some client-side disconnects)
    should_call_responder  = true;
    immediate_close_needed = true;
  }

  if (buf && buf->base) { // Ensure buf and buf->base are valid before freeing
    safe_free(buf->base); // Free the buffer allocated in alloc_buffer
  }

  if (should_call_responder && error_status_code != 0 && error_reason != NULL) {
    // Attempt to inform client. c4_http_respond will handle inactive handles.
    c4_http_respond(client, error_status_code, "text/plain", bytes(error_reason, strlen(error_reason)));
    // If c4_http_respond itself initiated a close (e.g. handle inactive, write failed), immediate_close_needed might be redundant
    // but close_client_connection is idempotent.
  }

  if (immediate_close_needed) {
    close_client_connection(client);
  }
  // If !immediate_close_needed, reading continues, or on_message_complete will be called.
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

void c4_write_error_response(client_t* client, int status, const char* error) {
  buffer_t buffer = {0};
  if (!error)
    buffer_add_chars(&buffer, "{\"error\":\"Internal error\"}");
  else if (error[0] == '{' && json_parse(error).type == JSON_TYPE_OBJECT)
    buffer_add_chars(&buffer, error);
  else
    bprintf(&buffer, "{\"error\":\"%S\"}", error);
  c4_http_respond(client, status, "application/json", buffer.data);
  buffer_free(&buffer);
  return;
}

void c4_http_respond(client_t* client, int status, char* content_type, bytes_t body) {
  // Only decrement if on_message_complete was reached for this request cycle
  if (client && client->message_complete_reached) {
    http_server.stats.open_requests--;
    // It will be reset in reset_client_request_data if connection is kept alive,
    // or doesn't matter if connection is closed.
    // For safety, and if not using reset_client_request_data in a path that calls this,
    // reset it here too for the non-keep-alive case, though it's redundant if on_close frees the client.
    // client->message_complete_reached = false; // Resetting here is an option, or in reset_client_request_data for keep-alive
  }

  if (!client) {
    fprintf(stderr, "ERROR: Attempted to respond to NULL client\n");
    return;
  }

  if (client->being_closed) {
    fprintf(stderr, "ERROR: Attempted to respond to a client that is already being closed\n");
    return;
  }

  if (!uv_is_active((uv_handle_t*) &client->handle)) {
    fprintf(stderr, "ERROR: Attempted to write to inactive client handle for client %p. Closing connection.\n", (void*) client);
    close_client_connection(client);
    return;
  }

  char     tmp[500];
  uv_buf_t uvbuf[2]; // Declared to be filled

  const char* conn_header_val = llhttp_should_keep_alive(&client->parser) ? "keep-alive" : "close";

  uvbuf[0].base = tmp;
  uvbuf[0].len  = snprintf(tmp, sizeof(tmp), "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: %s\r\n\r\n",
                           status, status_text(status), content_type, body.len, conn_header_val);

  uvbuf[1].base = (char*) body.data;
  uvbuf[1].len  = body.len;

  // Set client->being_closed based on the connection header *before* the write attempt.
  // This informs on_write_complete whether to close or reset for keep-alive.
  if (strcmp(conn_header_val, "close") == 0) {
    client->being_closed = true;
  }
  // If it's "keep-alive", client->being_closed remains false (its initial state or from a previous keep-alive cycle),
  // unless an error has already set it to true.

  // Set up a write callback to safely close the handle after writing or reset for keep-alive
  uv_write_t* write_req = &client->write_req;
  write_req->data       = client; // Ensure client is associated with write_req for on_write_complete

  int result = uv_write(write_req, (uv_stream_t*) &client->handle, uvbuf, 2, on_write_complete);

  if (result < 0) {
    fprintf(stderr, "ERROR: Failed to write HTTP response for client %p: %s\n", (void*) client, uv_strerror(result));
    close_client_connection(client);
    // on_write_complete will not be called, so we must close here.
  }
  // If write succeeds, on_write_complete will handle either closing or resetting for keep-alive.
}

// Callback for when a write completes - close the handle safely or reset for keep-alive
static void on_write_complete(uv_write_t* req, int status) {
  client_t* client = (client_t*) req->data;

  if (!client) {
    fprintf(stderr, "ERROR: client_t is NULL in on_write_complete\n");
    // Cannot do much here other than try to close the handle if req->handle is valid
    if (req->handle) {
      uv_close((uv_handle_t*) req->handle, NULL); // No specific on_close context
    }
    return;
  }

  if (status < 0) {
    fprintf(stderr, "ERROR: Write completed with error for client %p: %s\n", (void*) client, uv_strerror(status));
    // Ensure `being_closed` is true if a write error occurs, so it's closed below.
    client->being_closed = true;
  }

  // If client->being_closed is true (set by c4_http_respond for Connection:close, or due to write error),
  // or if status itself is an error, then close.
  if (client->being_closed) {
    close_client_connection(client);
  }
  else {
    // Keep-alive path: Reset client state for the next request
    reset_client_request_data(client);
    llhttp_reset(&client->parser);  // Reset parser for the next request on this connection
    client->keep_alive_idle = true; // Now connection is idle, awaiting next keep-alive request
    // The uv_read_start is presumably still active from c4_on_new_connection.
  }
}

void c4_on_new_connection(uv_stream_t* server, int status) {
  if (status < 0) {
    fprintf(stderr, "New connection error %s\n", uv_strerror(status));
    // No client object created yet to close.
    return;
  }
  uv_loop_t* loop   = server->loop;
  client_t*  client = (client_t*) safe_calloc(1, sizeof(client_t));
  uv_tcp_init(loop, &client->handle);
  client->handle.data              = client;
  client->being_closed             = false;
  client->message_complete_reached = false;
  client->keep_alive_idle          = false; // Initial state for new connections

  llhttp_settings_init(&client->settings);
  client->settings.on_url              = on_url;
  client->settings.on_method           = on_method;
  client->settings.on_header_field     = on_header_field;
  client->settings.on_header_value     = on_header_value;
  client->settings.on_body             = on_body;
  client->settings.on_message_complete = on_message_complete;

  llhttp_init(&client->parser, HTTP_REQUEST, &client->settings);
  client->parser.data = client;
  int err             = uv_accept(server, (uv_stream_t*) &client->handle);
  if (err == 0) {
    err = uv_read_start((uv_stream_t*) &client->handle, alloc_buffer, on_read);
  }

  if (err < 0) {
    const char* reason = uv_strerror(err);
    fprintf(stderr, "uv_accept/uv_read_start error for new client %p: %s\n", (void*) client, reason);
    c4_write_error_response(client, 500, reason);
    // Attempt to send an error response. c4_http_respond will handle inactive/problematic handles.
    // Ensure closure, as c4_http_respond might return if handle is inactive without calling on_write_complete.
    close_client_connection(client);
  }
}
