#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include "civetweb.h"
#include <stdlib.h>
#include <time.h>

// Forward declaration
struct pending_request;

// Status codes for HTTP client callbacks
typedef enum {
  HTTP_CLIENT_SUCCESS,          // Request completed successfully
  HTTP_CLIENT_CONNECTION_ERROR, // Failed to connect to server
  HTTP_CLIENT_TIMEOUT,          // Request timed out
  HTTP_CLIENT_MEMORY_ERROR,     // Memory allocation failed
  HTTP_CLIENT_READ_ERROR,       // Error reading from server
  HTTP_CLIENT_NO_RESPONSE       // No data received from server
} http_client_status_t;

// Callback function type for HTTP client requests
typedef void (*http_client_callback_t)(
    http_client_status_t    status,   // Status of the request
    struct pending_request* req,      // The request that completed
    void*                   user_data // User data passed to the callback
);

// Structure to track pending HTTP requests
struct pending_request {
  struct mg_connection*   client_conn;      // Original client connection
  struct mg_connection*   remote_conn;      // Connection to remote server
  time_t                  timestamp;        // When the request was created
  char*                   url;              // URL being fetched
  char*                   buffer;           // Response buffer
  size_t                  buffer_size;      // Current size of the buffer
  size_t                  buffer_len;       // Current length of data in buffer
  int                     request_sent;     // Flag indicating if request was sent
  int                     headers_parsed;   // Flag indicating if headers were parsed
  int                     content_length;   // Content length from headers
  struct pending_request* next;             // Next in linked list
  int                     debug_read_count; // Debug counter for read operations
  int                     is_ssl;           // Whether this is an SSL connection
  http_client_callback_t  callback;         // Callback function
  void*                   user_data;        // User data for callback
  int                     status_code;      // HTTP status code
};

// Process pending HTTP requests
void process_pending_requests(void);

// Start a non-blocking HTTP request with callback
struct pending_request* start_http_request_cb(
    struct mg_connection*  client_conn,
    const char*            url,
    http_client_callback_t callback,
    void*                  user_data);

// Start a non-blocking HTTP request (legacy function, uses the above with NULL callback)
struct pending_request* start_http_request(struct mg_connection* client_conn, const char* url);

// Get HTTP response buffer from request
const char* http_request_get_buffer(struct pending_request* req);

// Get HTTP response buffer length from request
size_t http_request_get_buffer_length(struct pending_request* req);

// Get HTTP status code from request
int http_request_get_status_code(struct pending_request* req);

// Get HTTP response body (content after headers)
const char* http_request_get_body(struct pending_request* req);

// Get HTTP response body length
size_t http_request_get_body_length(struct pending_request* req);

// Get specific header value from response
const char* http_request_get_header(struct pending_request* req, const char* header_name);

// Cancel a pending request
void cancel_http_request(struct pending_request* req);

// Parse URL to extract components
int parse_url(const char* url,
              char* scheme, size_t scheme_len,
              char* host, size_t host_len,
              int*  port,
              char* path, size_t path_len);

// Find header value in a HTTP response
char* find_header(const char* buffer, const char* header_name);

// Find the end of HTTP headers in a buffer
const char* find_headers_end(const char* buffer, size_t buffer_len);

// Debug function to print a portion of a buffer
void print_buffer_preview(const char* buffer, size_t len);

#endif /* HTTP_CLIENT_H */
