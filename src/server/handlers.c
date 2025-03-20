#include "handlers.h"
#include "http_client.h"
#include <stdio.h>
#include <string.h>

// Direct HTTP request to a test API that's known to work
int test_api_handler(struct mg_connection* conn, void* cbdata) {
  // Buffer to store the response
  char*  response_buffer      = NULL;
  size_t response_buffer_size = 0;

  // Try connecting to a simple test API
  char                  error_buffer[256] = {0};
  struct mg_connection* client            = mg_download(
      "httpbin.org", 80, 0,
      error_buffer, sizeof(error_buffer),
      "GET /json HTTP/1.1\r\n"
                 "Host: httpbin.org\r\n"
                 "User-Agent: civetweb-test/1.0\r\n"
                 "Accept: */*\r\n"
                 "Connection: close\r\n\r\n");

  if (client == NULL) {
    printf("Error connecting to httpbin: %s\n", error_buffer);
    mg_printf(conn,
              "HTTP/1.1 502 Bad Gateway\r\n"
              "Content-Type: text/plain\r\n"
              "Connection: close\r\n\r\n"
              "Error connecting to test API: %s",
              error_buffer);
    return 1;
  }

  // Read the response
  const struct mg_response_info* ri = mg_get_response_info(client);
  if (ri) {
    printf("Response status: %d %s\n", ri->status_code, ri->status_text);
    for (int i = 0; i < ri->num_headers; i++) {
      printf("Header: %s: %s\n", ri->http_headers[i].name, ri->http_headers[i].value);
    }
  }

  // Read data from client
  response_buffer_size = 16384;
  response_buffer      = (char*) malloc(response_buffer_size);
  if (!response_buffer) {
    mg_close_connection(client);
    mg_printf(conn,
              "HTTP/1.1 500 Internal Server Error\r\n"
              "Content-Type: text/plain\r\n"
              "Connection: close\r\n\r\n"
              "Memory allocation failed");
    return 1;
  }

  size_t bytes_read  = 0;
  size_t total_bytes = 0;
  char   read_buffer[4096];

  while ((bytes_read = mg_read(client, read_buffer, sizeof(read_buffer) - 1)) > 0) {
    // Resize buffer if needed
    if (total_bytes + bytes_read + 1 > response_buffer_size) {
      size_t new_size   = response_buffer_size * 2;
      char*  new_buffer = (char*) realloc(response_buffer, new_size);
      if (!new_buffer) {
        free(response_buffer);
        mg_close_connection(client);
        mg_printf(conn,
                  "HTTP/1.1 500 Internal Server Error\r\n"
                  "Content-Type: text/plain\r\n"
                  "Connection: close\r\n\r\n"
                  "Memory allocation failed");
        return 1;
      }
      response_buffer      = new_buffer;
      response_buffer_size = new_size;
    }

    // Copy data
    memcpy(response_buffer + total_bytes, read_buffer, bytes_read);
    total_bytes += bytes_read;
    response_buffer[total_bytes] = '\0';
  }

  mg_close_connection(client);

  // Send response to original client
  if (total_bytes > 0) {
    printf("Received %zu bytes from httpbin\n", total_bytes);

    // Check if we have headers
    const char* body_start = strstr(response_buffer, "\r\n\r\n");
    if (body_start) {
      // We have headers, forward the whole response
      mg_write(conn, response_buffer, total_bytes);
    }
    else {
      // No headers, construct our own
      mg_printf(conn,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n\r\n",
                total_bytes);
      mg_write(conn, response_buffer, total_bytes);
    }
  }
  else {
    mg_printf(conn,
              "HTTP/1.1 204 No Content\r\n"
              "Content-Type: text/plain\r\n"
              "Connection: close\r\n\r\n"
              "No data received from httpbin");
  }

  free(response_buffer);
  return 1;
}

// Try Lodestar API with the mg_download helper function
int lodestar_api_handler(struct mg_connection* conn, void* cbdata) {
  const struct mg_request_info* ri = mg_get_request_info(conn);

  // Check if request starts with /api/
  if (strncmp(ri->request_uri, "/api/", 5) != 0) {
    return 0; // Not our endpoint
  }

  // Extract path component after /api/
  const char* path = ri->request_uri + 5;

  // Buffer to store the response
  char*  response_buffer      = NULL;
  size_t response_buffer_size = 0;

  // Construct target URL path
  char target_path[1024];
  snprintf(target_path, sizeof(target_path), "/eth/v2/beacon/%s", path);

  printf("Forwarding to: https://lodestar-mainnet.chainsafe.io%s\n", target_path);

  // Try connecting using mg_download
  char                  error_buffer[256] = {0};
  struct mg_connection* client            = mg_download(
      "lodestar-mainnet.chainsafe.io", 443, 1, // using SSL
      error_buffer, sizeof(error_buffer),
      "GET %s HTTP/1.1\r\n"
                 "Host: lodestar-mainnet.chainsafe.io\r\n"
                 "User-Agent: curl/8.7.1\r\n"
                 "Accept: */*\r\n"
                 "Connection: close\r\n\r\n",
      target_path);

  if (client == NULL) {
    printf("Error connecting to Lodestar API: %s\n", error_buffer);
    mg_printf(conn,
              "HTTP/1.1 502 Bad Gateway\r\n"
              "Content-Type: text/plain\r\n"
              "Connection: close\r\n\r\n"
              "Error connecting to Lodestar API: %s",
              error_buffer);
    return 1;
  }

  // Read the response
  const struct mg_response_info* response_info = mg_get_response_info(client);
  if (response_info) {
    printf("Response status: %d %s\n", response_info->status_code, response_info->status_text);
    for (int i = 0; i < response_info->num_headers; i++) {
      printf("Header: %s: %s\n",
             response_info->http_headers[i].name,
             response_info->http_headers[i].value);
    }
  }
  else {
    printf("Failed to get response info\n");
  }

  // Read data from client
  response_buffer_size = 16384;
  response_buffer      = (char*) malloc(response_buffer_size);
  if (!response_buffer) {
    mg_close_connection(client);
    mg_printf(conn,
              "HTTP/1.1 500 Internal Server Error\r\n"
              "Content-Type: text/plain\r\n"
              "Connection: close\r\n\r\n"
              "Memory allocation failed");
    return 1;
  }

  size_t bytes_read  = 0;
  size_t total_bytes = 0;
  char   read_buffer[4096];

  while ((bytes_read = mg_read(client, read_buffer, sizeof(read_buffer) - 1)) > 0) {
    printf("Read %zu bytes from Lodestar\n", bytes_read);

    // Resize buffer if needed
    if (total_bytes + bytes_read + 1 > response_buffer_size) {
      size_t new_size   = response_buffer_size * 2;
      char*  new_buffer = (char*) realloc(response_buffer, new_size);
      if (!new_buffer) {
        free(response_buffer);
        mg_close_connection(client);
        mg_printf(conn,
                  "HTTP/1.1 500 Internal Server Error\r\n"
                  "Content-Type: text/plain\r\n"
                  "Connection: close\r\n\r\n"
                  "Memory allocation failed");
        return 1;
      }
      response_buffer      = new_buffer;
      response_buffer_size = new_size;
    }

    // Copy data
    memcpy(response_buffer + total_bytes, read_buffer, bytes_read);
    total_bytes += bytes_read;
    response_buffer[total_bytes] = '\0';
  }

  printf("Done reading from Lodestar, got %zu bytes\n", total_bytes);
  mg_close_connection(client);

  // Send response to original client
  if (total_bytes > 0) {
    printf("Sending %zu bytes to client\n", total_bytes);

    // Check if we have headers
    const char* body_start = strstr(response_buffer, "\r\n\r\n");
    if (body_start) {
      // We have headers, forward the whole response
      mg_write(conn, response_buffer, total_bytes);
    }
    else {
      // No headers, construct our own
      mg_printf(conn,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n\r\n",
                total_bytes);
      mg_write(conn, response_buffer, total_bytes);
    }
  }
  else {
    mg_printf(conn,
              "HTTP/1.1 204 No Content\r\n"
              "Content-Type: text/plain\r\n"
              "Connection: close\r\n\r\n"
              "No data received from Lodestar API");
  }

  free(response_buffer);
  return 1;
}