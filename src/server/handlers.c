#include "handlers.h"
#include "http_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Callback for test_api_handler
static void test_api_callback(http_client_status_t status, struct pending_request* req, void* user_data) {
  struct mg_connection* conn = (struct mg_connection*) user_data;

  if (status == HTTP_CLIENT_SUCCESS) {
    // Successfully received response
    printf("Received response from httpbin.org\n");

    // Get the response data - prefer to get just the body if possible
    const char* body     = http_request_get_body(req);
    size_t      body_len = http_request_get_body_length(req);

    // Fallback to full buffer if body extraction failed
    if (!body || body_len == 0) {
      body     = http_request_get_buffer(req);
      body_len = http_request_get_buffer_length(req);
      printf("Using full response buffer as body extraction failed\n");
    }
    else {
      printf("Successfully extracted response body (%zu bytes)\n", body_len);
    }

    if (body && body_len > 0) {
      // Send response to client with proper headers
      mg_printf(conn,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n\r\n",
                body_len);
      mg_write(conn, body, body_len);
    }
    else {
      // No data received
      mg_printf(conn,
                "HTTP/1.1 204 No Content\r\n"
                "Content-Type: text/plain\r\n"
                "Connection: close\r\n\r\n"
                "No data received from httpbin");
    }
  }
  else {
    // Error handling
    const char* error_msg = "Unknown error";

    switch (status) {
      case HTTP_CLIENT_CONNECTION_ERROR:
        error_msg = "Connection error";
        break;
      case HTTP_CLIENT_TIMEOUT:
        error_msg = "Request timed out";
        break;
      case HTTP_CLIENT_MEMORY_ERROR:
        error_msg = "Memory allocation failed";
        break;
      case HTTP_CLIENT_READ_ERROR:
        error_msg = "Error reading from server";
        break;
      case HTTP_CLIENT_NO_RESPONSE:
        error_msg = "No response from server";
        break;
      default:
        error_msg = "Unknown error";
        break;
    }

    mg_printf(conn,
              "HTTP/1.1 502 Bad Gateway\r\n"
              "Content-Type: text/plain\r\n"
              "Connection: close\r\n\r\n"
              "Error accessing test API: %s",
              error_msg);
  }
}

// Direct HTTP request to a test API that's known to work - Non-blocking version
int test_api_handler(struct mg_connection* conn, void* cbdata) {
  printf("Starting test_api_handler using non-blocking approach\n");

  // Start a non-blocking HTTP request with callback
  start_http_request_cb(
      conn,
      "http://httpbin.org/json",
      test_api_callback,
      conn // Pass the original connection as user data
  );

  // Return immediately, response will be sent from the callback
  return 1;
}

// Try Lodestar API with direct mg_download call for testing
int lodestar_api_handler(struct mg_connection* conn, void* cbdata) {
  const struct mg_request_info* ri = mg_get_request_info(conn);

  // Check if request starts with /api/
  if (strncmp(ri->request_uri, "/api/", 5) != 0) {
    return 0; // Not our endpoint
  }

  // Extract path component after /api/
  const char* path = ri->request_uri + 5;

  // Parse URL components for httpbin.org
  char host[256]          = "httpbin.org";
  int  port               = 443;
  int  use_ssl            = 1;
  char request_path[1024] = "/";

  // Use the path from the request or default to /get
  if (*path) {
    snprintf(request_path, sizeof(request_path), "/%s", path);
  }
  else {
    strcpy(request_path, "/get");
  }

  printf("TESTING - Using mg_download directly: %s:%d%s (SSL: %s)\n",
         host, port, request_path, use_ssl ? "yes" : "no");

  // NOTE: We're using mg_download directly here instead of our non-blocking implementation
  // because our custom SSL handling in the non-blocking approach has issues.
  // This is a temporary solution - in a production environment, we would need to:
  // 1. Fix the SSL handling in our non-blocking implementation
  // 2. OR implement a state machine that uses mg_download but doesn't block the main thread
  // 3. OR use a dedicated thread pool for handling HTTPS requests
  // The current implementation blocks the server thread while waiting for the response,
  // which is not ideal for a production server.

  // Try direct download with mg_download (blocking)
  char error_buffer[256] = {0};

  // Create the HTTP request
  char request[2048];
  snprintf(request, sizeof(request),
           "GET %s HTTP/1.1\r\n"
           "Host: %s\r\n"
           "User-Agent: CivetWeb-Client/1.0\r\n"
           "Accept: */*\r\n"
           "Connection: close\r\n"
           "\r\n",
           request_path, host);

  printf("Sending request:\n%s\n", request);

  // Use mg_download to make the request
  struct mg_connection* remote_conn = mg_download(
      host,                 // Host
      port,                 // Port
      use_ssl,              // Use SSL
      error_buffer,         // Error buffer
      sizeof(error_buffer), // Error buffer size
      "%s",                 // Format string
      request               // The request itself
  );

  if (remote_conn == NULL) {
    // Error handling
    printf("Error from mg_download: %s\n", error_buffer);
    mg_printf(conn,
              "HTTP/1.1 502 Bad Gateway\r\n"
              "Content-Type: text/plain\r\n"
              "Connection: close\r\n\r\n"
              "Error accessing API: %s",
              error_buffer);
    return 1;
  }

  printf("Connection established, reading response...\n");

  // Read the response data
  char response_buffer[16384] = {0};
  int  total_bytes_read       = 0;
  int  bytes_read;
  int  response_code = 0;

  // Read data in chunks
  while ((bytes_read = mg_read(remote_conn,
                               response_buffer + total_bytes_read,
                               sizeof(response_buffer) - total_bytes_read - 1)) > 0) {
    total_bytes_read += bytes_read;
    response_buffer[total_bytes_read] = '\0'; // Null-terminate

    printf("Read %d bytes (total: %d)\n", bytes_read, total_bytes_read);

    // Check if we've got headers yet
    if (response_code == 0) {
      // Try to parse status code
      if (strncmp(response_buffer, "HTTP/", 5) == 0) {
        // Find the status code after "HTTP/x.x "
        char* status_start = strchr(response_buffer, ' ');
        if (status_start) {
          response_code = atoi(status_start + 1);
          printf("HTTP status code: %d\n", response_code);
        }
      }
    }

    // Check if buffer is getting full
    if (sizeof(response_buffer) - total_bytes_read - 1 < 1024) {
      printf("Buffer getting full, stopping\n");
      break;
    }
  }

  printf("Finished reading, got %d bytes total\n", total_bytes_read);

  // Close the connection
  mg_close_connection(remote_conn);

  if (total_bytes_read <= 0) {
    // No data received
    printf("No data received\n");
    mg_printf(conn,
              "HTTP/1.1 502 Bad Gateway\r\n"
              "Content-Type: text/plain\r\n"
              "Connection: close\r\n\r\n"
              "No data received from API");
    return 1;
  }

  // Extract the body
  const char* body = strstr(response_buffer, "\r\n\r\n");
  if (body) {
    // Normal HTTP response with headers
    body += 4;
    int body_size = response_buffer + total_bytes_read - body;
    printf("Found body: %d bytes\n", body_size);

    mg_printf(conn,
              "HTTP/1.1 %d OK\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: %d\r\n"
              "Connection: close\r\n\r\n",
              response_code, body_size);
    mg_write(conn, body, body_size);
  }
  else if (response_buffer[0] == '{') {
    // Raw JSON response without headers
    printf("Response appears to be raw JSON without headers\n");

    mg_printf(conn,
              "HTTP/1.1 %d OK\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: %d\r\n"
              "Connection: close\r\n\r\n",
              response_code, total_bytes_read);
    mg_write(conn, response_buffer, total_bytes_read);
  }
  else {
    printf("Failed to find body separator in response\n");
    // For debugging, print part of the response
    printf("Response preview: %.100s\n", response_buffer);

    mg_printf(conn,
              "HTTP/1.1 502 Bad Gateway\r\n"
              "Content-Type: text/plain\r\n"
              "Connection: close\r\n\r\n"
              "Failed to extract response body");
  }

  return 1;
}

// State machine for handling multiple sequential HTTP requests
typedef enum {
  STATE_INITIAL,        // Initial state
  STATE_FIRST_REQUEST,  // First API request in progress
  STATE_SECOND_REQUEST, // Second API request in progress
  STATE_DONE            // All processing complete
} state_machine_state_t;

// Context data for state machine handler
typedef struct state_machine_context {
  struct mg_connection* client_conn;    // Original client connection
  state_machine_state_t state;          // Current state
  char*                 first_response; // Buffer for first response
  size_t                first_response_len;
  char*                 second_response; // Buffer for second response
  size_t                second_response_len;
} state_machine_context_t;

// Forward declaration for the state machine callbacks
static void state_machine_first_callback(http_client_status_t status, struct pending_request* req, void* user_data);
static void state_machine_second_callback(http_client_status_t status, struct pending_request* req, void* user_data);

// First callback in the state machine
static void state_machine_first_callback(http_client_status_t status, struct pending_request* req, void* user_data) {
  state_machine_context_t* context = (state_machine_context_t*) user_data;

  printf("First request callback - status: %d\n", status);

  if (status == HTTP_CLIENT_SUCCESS) {
    // Successfully received response from first API
    // Try to get just the body first
    const char* buffer     = http_request_get_body(req);
    size_t      buffer_len = http_request_get_body_length(req);

    // If body extraction failed, use full response
    if (!buffer || buffer_len == 0) {
      buffer     = http_request_get_buffer(req);
      buffer_len = http_request_get_buffer_length(req);
    }

    // Store the response
    context->first_response = (char*) malloc(buffer_len + 1);
    if (context->first_response) {
      memcpy(context->first_response, buffer, buffer_len);
      context->first_response[buffer_len] = '\0';
      context->first_response_len         = buffer_len;
      printf("Stored first response: %zu bytes\n", buffer_len);

      // Transition to next state - make second request
      context->state = STATE_SECOND_REQUEST;

      // Start the second request (to a different API)
      printf("Starting second API request...\n");
      start_http_request_cb(
          context->client_conn,
          "http://httpbin.org/uuid",
          state_machine_second_callback,
          context);
    }
    else {
      // Memory allocation failed
      mg_printf(context->client_conn,
                "HTTP/1.1 500 Internal Server Error\r\n"
                "Content-Type: text/plain\r\n"
                "Connection: close\r\n\r\n"
                "Memory allocation failed");

      // Clean up and set done state
      context->state = STATE_DONE;
      free(context);
    }
  }
  else {
    // Error handling - send error response to client
    mg_printf(context->client_conn,
              "HTTP/1.1 502 Bad Gateway\r\n"
              "Content-Type: text/plain\r\n"
              "Connection: close\r\n\r\n"
              "Error fetching data from first API");

    // Clean up and set done state
    context->state = STATE_DONE;
    free(context);
  }
}

// Second callback in the state machine
static void state_machine_second_callback(http_client_status_t status, struct pending_request* req, void* user_data) {
  state_machine_context_t* context = (state_machine_context_t*) user_data;

  printf("Second request callback - status: %d\n", status);

  if (status == HTTP_CLIENT_SUCCESS) {
    // Successfully received response from second API
    // Try to get just the body first
    const char* buffer     = http_request_get_body(req);
    size_t      buffer_len = http_request_get_body_length(req);

    // If body extraction failed, use full response
    if (!buffer || buffer_len == 0) {
      buffer     = http_request_get_buffer(req);
      buffer_len = http_request_get_buffer_length(req);
    }

    // Store the response
    context->second_response = (char*) malloc(buffer_len + 1);
    if (context->second_response) {
      memcpy(context->second_response, buffer, buffer_len);
      context->second_response[buffer_len] = '\0';
      context->second_response_len         = buffer_len;
      printf("Stored second response: %zu bytes\n", buffer_len);

      // Process both responses and send combined result to client
      mg_printf(context->client_conn,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Connection: close\r\n\r\n"
                "{\n"
                "  \"first_api\": %.*s,\n"
                "  \"second_api\": %.*s\n"
                "}\n",
                (int) context->first_response_len,
                context->first_response,
                (int) context->second_response_len,
                context->second_response);
    }
    else {
      // Memory allocation failed
      mg_printf(context->client_conn,
                "HTTP/1.1 500 Internal Server Error\r\n"
                "Content-Type: text/plain\r\n"
                "Connection: close\r\n\r\n"
                "Memory allocation failed");
    }
  }
  else {
    // Error handling - at least return the first response
    mg_printf(context->client_conn,
              "HTTP/1.1 206 Partial Content\r\n"
              "Content-Type: application/json\r\n"
              "Connection: close\r\n\r\n"
              "{\n"
              "  \"first_api\": %.*s,\n"
              "  \"second_api\": null,\n"
              "  \"error\": \"Failed to fetch data from second API\"\n"
              "}\n",
              (int) context->first_response_len,
              context->first_response);
  }

  // Clean up resources
  if (context->first_response) {
    free(context->first_response);
  }
  if (context->second_response) {
    free(context->second_response);
  }

  // Set state to done
  context->state = STATE_DONE;
  free(context);
}

// State machine handler that demonstrates using callbacks with the HTTP client
int statemachine_handler(struct mg_connection* conn, void* cbdata) {
  // Use direct mg_download for testing
  char error_buffer[256] = {0};
  char host[]            = "httpbin.org";
  int  port              = 443; // Change to HTTPS port
  int  use_ssl           = 1;   // Enable SSL

  printf("STATEMACHINE: Using direct mg_download for https://httpbin.org/json\n");

  // Create HTTP request
  char request[512];
  snprintf(request, sizeof(request),
           "GET /json HTTP/1.1\r\n"
           "Host: httpbin.org\r\n"
           "User-Agent: CivetWeb-Client/1.0\r\n"
           "Accept: */*\r\n"
           "Connection: close\r\n"
           "\r\n");

  // Use mg_download to make the request
  struct mg_connection* remote_conn = mg_download(
      host,                 // Host
      port,                 // Port
      use_ssl,              // Use SSL
      error_buffer,         // Error buffer
      sizeof(error_buffer), // Error buffer size
      "%s",                 // Format string
      request               // The request itself
  );

  if (remote_conn == NULL) {
    mg_printf(conn,
              "HTTP/1.1 502 Bad Gateway\r\n"
              "Content-Type: text/plain\r\n"
              "Connection: close\r\n\r\n"
              "Error accessing httpbin.org: %s",
              error_buffer);
    return 1;
  }

  // Read the response
  char response_buffer[16384] = {0};
  int  total_bytes_read       = 0;
  int  bytes_read;

  // Add more debugging
  printf("Connection established, reading response...\n");

  while ((bytes_read = mg_read(remote_conn,
                               response_buffer + total_bytes_read,
                               sizeof(response_buffer) - total_bytes_read - 1)) > 0) {
    total_bytes_read += bytes_read;
    response_buffer[total_bytes_read] = '\0';
    printf("Read %d bytes (total: %d)\n", bytes_read, total_bytes_read);
  }

  printf("Finished reading, got %d bytes total\n", total_bytes_read);
  mg_close_connection(remote_conn);

  if (total_bytes_read <= 0) {
    // No data received
    printf("No data received\n");
    mg_printf(conn,
              "HTTP/1.1 502 Bad Gateway\r\n"
              "Content-Type: text/plain\r\n"
              "Connection: close\r\n\r\n"
              "No data received from httpbin.org");
    return 1;
  }

  // Extract the body
  const char* body = strstr(response_buffer, "\r\n\r\n");
  if (body) {
    // Normal HTTP response with headers
    body += 4;
    int body_size = response_buffer + total_bytes_read - body;
    printf("Found body: %d bytes\n", body_size);

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: %d\r\n"
              "Connection: close\r\n\r\n",
              body_size);
    mg_write(conn, body, body_size);
  }
  else if (response_buffer[0] == '{') {
    // Raw JSON response without headers
    printf("Response appears to be raw JSON without headers\n");

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: %d\r\n"
              "Connection: close\r\n\r\n",
              total_bytes_read);
    mg_write(conn, response_buffer, total_bytes_read);
  }
  else {
    printf("Failed to find body separator in response\n");
    // For debugging, print part of the response
    printf("Response preview: %.100s\n", response_buffer);

    mg_printf(conn,
              "HTTP/1.1 502 Bad Gateway\r\n"
              "Content-Type: text/plain\r\n"
              "Connection: close\r\n\r\n"
              "Failed to extract response body");
  }

  return 1;
}