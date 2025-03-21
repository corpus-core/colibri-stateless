#include "http_client.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#endif

// Global list of pending requests
static struct pending_request* pending_requests = NULL;

// Add a pending request to the global list
static void add_pending_request(struct pending_request* req) {
  req->next        = pending_requests;
  pending_requests = req;
}

// Helper function to call the callback with the given status
static void invoke_callback(struct pending_request* req, http_client_status_t status) {
  if (req->callback) {
    req->callback(status, req, req->user_data);
  }
}

// Remove a pending request from the global list
static void remove_pending_request(struct pending_request* req) {
  if (pending_requests == req) {
    pending_requests = req->next;
  }
  else {
    struct pending_request* current = pending_requests;
    while (current && current->next != req) {
      current = current->next;
    }
    if (current) {
      current->next = req->next;
    }
  }

  // Close connection and free resources (except the client_conn which is managed by the caller)
  if (req->remote_conn) {
    mg_close_connection(req->remote_conn);
    req->remote_conn = NULL;
  }
}

// Free resources associated with a request
void cancel_http_request(struct pending_request* req) {
  if (!req) return;

  // First, remove from the pending list
  remove_pending_request(req);

  // Free all resources
  if (req->buffer) {
    free(req->buffer);
  }
  if (req->url) {
    free(req->url);
  }
  free(req);
}

// Parse URL to extract components
int parse_url(const char* url,
              char* scheme, size_t scheme_len,
              char* host, size_t host_len,
              int*  port,
              char* path, size_t path_len) {

  // Default values
  strcpy(scheme, "http");
  *port = 80;
  strcpy(path, "/");

  // Check URL scheme
  if (strncmp(url, "https://", 8) == 0) {
    strcpy(scheme, "https");
    url += 8; // Skip "https://"
    *port = 443;
  }
  else if (strncmp(url, "http://", 7) == 0) {
    strcpy(scheme, "http");
    url += 7; // Skip "http://"
    *port = 80;
  }
  else {
    return 0; // Invalid URL
  }

  // Find end of hostname (either '/' or ':')
  const char* p1 = strchr(url, '/');
  const char* p2 = strchr(url, ':');

  if (p2 != NULL && (p1 == NULL || p2 < p1)) {
    // Host with port specified
    size_t host_size = p2 - url;
    if (host_size >= host_len) return 0; // Host too long
    strncpy(host, url, host_size);
    host[host_size] = '\0';

    // Extract port
    *port = atoi(p2 + 1);

    // Extract path if present
    if (p1 != NULL) {
      strncpy(path, p1, path_len - 1);
      path[path_len - 1] = '\0';
    }
  }
  else if (p1 != NULL) {
    // Host with path
    size_t host_size = p1 - url;
    if (host_size >= host_len) return 0; // Host too long
    strncpy(host, url, host_size);
    host[host_size] = '\0';

    // Extract path
    strncpy(path, p1, path_len - 1);
    path[path_len - 1] = '\0';
  }
  else {
    // Just host
    strncpy(host, url, host_len - 1);
    host[host_len - 1] = '\0';
  }

  return 1; // Success
}

// Debug function to print a portion of a buffer
void print_buffer_preview(const char* buffer, size_t len) {
  printf("Buffer (%zu bytes): ", len);
  size_t preview_len = len < 100 ? len : 100; // Show first 100 bytes max
  for (size_t i = 0; i < preview_len; i++) {
    if (buffer[i] >= 32 && buffer[i] <= 126) { // Printable ASCII
      putchar(buffer[i]);
    }
    else {
      printf("\\x%02x", (unsigned char) buffer[i]);
    }
  }
  if (len > preview_len) {
    printf("... (%zu more bytes)", len - preview_len);
  }
  printf("\n");
}

// Helper function to extract body from response buffer
const char* http_request_get_body(struct pending_request* req) {
  if (!req || !req->buffer || req->buffer_len == 0) {
    return NULL;
  }

  const char* headers_end = find_headers_end(req->buffer, req->buffer_len);
  if (!headers_end) {
    return NULL; // No end of headers found
  }

  // Skip the \r\n\r\n separator
  return headers_end + 4;
}

// Get HTTP body length
size_t http_request_get_body_length(struct pending_request* req) {
  if (!req || !req->buffer || req->buffer_len == 0) {
    return 0;
  }

  const char* body = http_request_get_body(req);
  if (!body) {
    return 0;
  }

  // Calculate body length as total length minus offset of body start
  return req->buffer_len - (body - req->buffer);
}

// Get specific header value from response
const char* http_request_get_header(struct pending_request* req, const char* header_name) {
  if (!req || !req->buffer || !header_name) {
    return NULL;
  }

  return find_header(req->buffer, header_name);
}

// Start a non-blocking HTTP request with callback
struct pending_request* start_http_request_cb(
    struct mg_connection*  client_conn,
    const char*            url,
    http_client_callback_t callback,
    void*                  user_data) {

  printf("Starting HTTP request to %s with callback\n", url);

  // Parse the URL
  char scheme[16] = {0};
  char host[256]  = {0};
  char path[1024] = {0};
  int  port       = 80;

  if (!parse_url(url, scheme, sizeof(scheme), host, sizeof(host), &port, path, sizeof(path))) {
    printf("Failed to parse URL: %s\n", url);
    // If callback provided, call it with error status
    if (callback) {
      struct pending_request tmp = {0};
      tmp.callback               = callback;
      tmp.user_data              = user_data;
      invoke_callback(&tmp, HTTP_CLIENT_CONNECTION_ERROR);
    }
    return NULL;
  }

  // Use SSL for HTTPS
  int use_ssl = (strcmp(scheme, "https") == 0);

  // Debug log
  printf("Connecting to %s:%d %s (SSL: %s)\n",
         host, port, path, use_ssl ? "yes" : "no");

  // Connect to the remote server - with more detailed error output
  char error_buffer[512] = {0};
  printf("Attempting to connect to %s:%d using %s\n",
         host, port, use_ssl ? "SSL/TLS" : "plain HTTP");

  struct mg_connection* remote_conn = mg_connect_client(
      host, port, use_ssl, error_buffer, sizeof(error_buffer));

  if (remote_conn == NULL) {
    printf("Failed to connect to %s:%d: %s\n", host, port, error_buffer);
    // If callback provided, call it with error status
    if (callback) {
      struct pending_request tmp = {0};
      tmp.callback               = callback;
      tmp.user_data              = user_data;
      invoke_callback(&tmp, HTTP_CLIENT_CONNECTION_ERROR);
    }
    return NULL;
  }

  printf("Successfully connected to %s:%d\n", host, port);

  // For SSL connections, check if handshake was successful
  if (use_ssl) {
    printf("SSL connection established, checking handshake status\n");
    // You could add specific SSL handshake verification here
    // Note: This requires CivetWeb's SSL functions, which might not be directly accessible
  }

  // Allocate memory for the new request
  struct pending_request* req = (struct pending_request*) calloc(1, sizeof(struct pending_request));
  if (!req) {
    mg_close_connection(remote_conn);
    printf("Failed to allocate memory for request\n");
    // If callback provided, call it with error status
    if (callback) {
      struct pending_request tmp = {0};
      tmp.callback               = callback;
      tmp.user_data              = user_data;
      invoke_callback(&tmp, HTTP_CLIENT_MEMORY_ERROR);
    }
    return NULL;
  }

  // Initialize the request structure
  req->client_conn      = client_conn;
  req->remote_conn      = remote_conn;
  req->timestamp        = time(NULL);
  req->url              = strdup(url);
  req->buffer_size      = 16384; // Initial buffer size
  req->buffer           = (char*) malloc(req->buffer_size);
  req->debug_read_count = 0;
  req->callback         = callback;
  req->user_data        = user_data;
  req->status_code      = 0;
  req->is_ssl           = use_ssl;

  if (!req->buffer || !req->url) {
    if (req->buffer) free(req->buffer);
    if (req->url) free(req->url);
    free(req);
    mg_close_connection(remote_conn);
    printf("Failed to allocate memory for request buffers\n");
    // If callback provided, call it with error status
    if (callback) {
      struct pending_request tmp = {0};
      tmp.callback               = callback;
      tmp.user_data              = user_data;
      invoke_callback(&tmp, HTTP_CLIENT_MEMORY_ERROR);
    }
    return NULL;
  }

  // Construct and send the HTTP request
  char request[2048];
  // If path is empty, use /
  if (path[0] == '\0') {
    snprintf(path, sizeof(path), "/");
  }

  // Create a more complete HTTP request with standard headers
  snprintf(request, sizeof(request),
           "GET %s HTTP/1.1\r\n"
           "Host: %s\r\n"
           "User-Agent: CivetWeb-Client/1.0\r\n"
           "Accept: */*\r\n"
           "Connection: close\r\n"
           "\r\n",
           path, host);

  printf("Sending request:\n%s\n", request);

  // Send the request
  int bytes_sent = mg_write(remote_conn, request, strlen(request));
  if (bytes_sent <= 0) {
    free(req->buffer);
    free(req->url);
    free(req);
    mg_close_connection(remote_conn);
    printf("Failed to send request\n");
    // If callback provided, call it with error status
    if (callback) {
      struct pending_request tmp = {0};
      tmp.callback               = callback;
      tmp.user_data              = user_data;
      invoke_callback(&tmp, HTTP_CLIENT_CONNECTION_ERROR);
    }
    return NULL;
  }

  printf("Request sent successfully (%d bytes)\n", bytes_sent);
  req->request_sent = 1;

  // Add the request to the list of pending requests
  add_pending_request(req);

  printf("HTTP request started, waiting for response\n");
  return req;
}

// Legacy function for backward compatibility
struct pending_request* start_http_request(struct mg_connection* client_conn, const char* url) {
  return start_http_request_cb(client_conn, url, NULL, NULL);
}

// Get HTTP response buffer from request
const char* http_request_get_buffer(struct pending_request* req) {
  return req ? req->buffer : NULL;
}

// Get HTTP response buffer length from request
size_t http_request_get_buffer_length(struct pending_request* req) {
  return req ? req->buffer_len : 0;
}

// Get HTTP status code from request
int http_request_get_status_code(struct pending_request* req) {
  return req ? req->status_code : 0;
}

// Find header value in a HTTP response
char* find_header(const char* buffer, const char* header_name) {
  if (!buffer || !header_name) {
    return NULL;
  }

  char  header[256];
  char* value    = NULL;
  int   name_len = strlen(header_name);

  snprintf(header, sizeof(header), "\r\n%s:", header_name);

  // Use case-insensitive search (this is a naive implementation)
  // strcasestr would be better but may not be available on all platforms
  char* lower_buffer = strdup(buffer);
  if (!lower_buffer) {
    return NULL;
  }
  char lower_header[256];
  strcpy(lower_header, header);

  // Convert both to lowercase
  for (char* p = lower_buffer; *p; p++) {
    *p = tolower(*p);
  }
  for (char* p = lower_header; *p; p++) {
    *p = tolower(*p);
  }

  value = strstr(lower_buffer, lower_header);

  if (!value) {
    // Check if the header is at the start of the buffer (no preceding CRLF)
    snprintf(lower_header, sizeof(lower_header), "%s:", header_name);
    for (char* p = lower_header; *p; p++) {
      *p = tolower(*p);
    }

    if (strncmp(lower_buffer, lower_header, strlen(lower_header)) == 0) {
      value = (char*) buffer; // Point to the original buffer
    }
    else {
      free(lower_buffer);
      return NULL;
    }
  }
  else {
    // Calculate offset in the original buffer
    value = (char*) buffer + (value - lower_buffer);
    // Skip the CRLF
    value += 2;
  }

  free(lower_buffer);

  // Skip the header name and colon
  value += name_len + 1;

  // Skip spaces
  while (*value == ' ') {
    value++;
  }

  // Find end of header (CRLF)
  char* end = strstr(value, "\r\n");
  if (end) {
    *end         = '\0'; // Temporarily terminate the string
    char* result = strdup(value);
    *end         = '\r'; // Restore the original buffer
    return result;
  }

  return strdup(value);
}

// Parse status code from HTTP response
static int parse_status_code(const char* buffer) {
  // Find the first space after "HTTP/"
  const char* http_ver = strstr(buffer, "HTTP/");
  if (!http_ver) return 0;

  const char* status_start = strchr(http_ver, ' ');
  if (!status_start) return 0;

  // Skip spaces
  while (*status_start == ' ') status_start++;

  // Parse the status code
  return atoi(status_start);
}

// Find the end of HTTP headers in a buffer
const char* find_headers_end(const char* buffer, size_t buffer_len) {
  const char* end = strstr(buffer, "\r\n\r\n");
  if (end && (size_t) (end - buffer + 4) <= buffer_len) {
    return end + 4; // Skip "\r\n\r\n"
  }
  return NULL;
}

// Process pending requests in event loop
void process_pending_requests() {
  struct pending_request* req  = pending_requests;
  struct pending_request* prev = NULL;
  struct pending_request* next = NULL;
  time_t                  now  = time(NULL);

  while (req) {
    next = req->next;

    // Check for timeout (30 seconds)
    if (now - req->timestamp > 30) {
      printf("Request to %s timed out\n", req->url);

      // Remove from list
      if (prev) {
        prev->next = next;
      }
      else {
        pending_requests = next;
      }

      // Invoke callback with timeout status
      invoke_callback(req, HTTP_CLIENT_TIMEOUT);

      // Cleanup
      mg_close_connection(req->remote_conn);
      free(req->buffer);
      free(req->url);
      free(req);

      req = next;
      continue;
    }

    // Check if we can read from the remote connection
    char read_buffer[4096];
    printf("Attempting to read data from %s (SSL: %s)\n",
           req->url, req->is_ssl ? "yes" : "no");

    int bytes_read = mg_read(req->remote_conn, read_buffer, sizeof(read_buffer) - 1);

    if (bytes_read > 0) {
      req->debug_read_count++;
      printf("Read %d bytes from %s (read #%d)\n", bytes_read, req->url, req->debug_read_count);

      // Debug: Print the first few bytes of the response
      printf("First %d bytes of response: ", bytes_read < 100 ? bytes_read : 100);
      for (int i = 0; i < bytes_read && i < 100; i++) {
        if (isprint(read_buffer[i])) {
          putchar(read_buffer[i]);
        }
        else {
          printf("\\x%02x", (unsigned char) read_buffer[i]);
        }
      }
      printf("\n");

      // Expand buffer if needed
      if (req->buffer_len + bytes_read + 1 > req->buffer_size) {
        size_t new_size   = req->buffer_size * 2;
        char*  new_buffer = (char*) realloc(req->buffer, new_size);
        if (!new_buffer) {
          printf("Failed to expand buffer for request to %s\n", req->url);

          // Remove from list
          if (prev) {
            prev->next = next;
          }
          else {
            pending_requests = next;
          }

          // Invoke callback with memory error status
          invoke_callback(req, HTTP_CLIENT_MEMORY_ERROR);

          // Cleanup
          mg_close_connection(req->remote_conn);
          free(req->buffer);
          free(req->url);
          free(req);

          req = next;
          continue;
        }

        req->buffer      = new_buffer;
        req->buffer_size = new_size;
      }

      // Copy data to buffer
      memcpy(req->buffer + req->buffer_len, read_buffer, bytes_read);
      req->buffer_len += bytes_read;
      req->buffer[req->buffer_len] = '\0';

      // Update timestamp to avoid timeout while receiving data
      req->timestamp = now;

      // If headers not parsed yet, try to parse them
      if (!req->headers_parsed) {
        const char* headers_end = find_headers_end(req->buffer, req->buffer_len);
        if (headers_end) {
          req->headers_parsed = 1;

          // Parse the status code from the first line
          // Example: "HTTP/1.1 200 OK\r\n"
          char status_line[256];
          int  i = 0;
          while (i < sizeof(status_line) - 1 && i < req->buffer_len && req->buffer[i] != '\r' && req->buffer[i] != '\n') {
            status_line[i] = req->buffer[i];
            i++;
          }
          status_line[i] = '\0';

          char* status_code_str = strstr(status_line, " ");
          if (status_code_str) {
            req->status_code = atoi(status_code_str + 1);
            printf("HTTP status code: %d\n", req->status_code);
          }

          // Find Content-Length header
          char* content_length = find_header(req->buffer, "Content-Length");
          if (content_length) {
            req->content_length = atoi(content_length);
            printf("Content-Length: %d\n", req->content_length);
            free(content_length); // Free the header value
          }
          else {
            req->content_length = -1; // Unknown content length
          }
        }
      }

      // Continue to next request
      prev = req;
      req  = next;
      continue;
    }
    else if (bytes_read == 0) {
      // End of transmission
      printf("Connection closed for request to %s\n", req->url);

      // Remove from list
      if (prev) {
        prev->next = next;
      }
      else {
        pending_requests = next;
      }

      // Check if we got any data
      if (req->buffer_len > 0) {
        printf("Received total %zu bytes\n", req->buffer_len);
        // Invoke callback with success status
        invoke_callback(req, HTTP_CLIENT_SUCCESS);
      }
      else {
        printf("No data received before connection closed (SSL: %s)\n",
               req->is_ssl ? "yes" : "no");
        // Invoke callback with no data status
        invoke_callback(req, HTTP_CLIENT_NO_RESPONSE);
      }

      // Cleanup
      mg_close_connection(req->remote_conn);
      free(req->buffer);
      free(req->url);
      free(req);

      req = next;
      continue;
    }
    else if (bytes_read < 0) {
      printf("Error reading from %s (SSL: %s)\n",
             req->url, req->is_ssl ? "yes" : "no");

      // Remove from list
      if (prev) {
        prev->next = next;
      }
      else {
        pending_requests = next;
      }

      // Invoke callback with read error status
      invoke_callback(req, HTTP_CLIENT_READ_ERROR);

      // Cleanup
      mg_close_connection(req->remote_conn);
      free(req->buffer);
      free(req->url);
      free(req);

      req = next;
      continue;
    }

    // Continue to next request
    prev = req;
    req  = next;
  }
}