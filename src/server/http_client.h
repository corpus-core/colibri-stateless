#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include "civetweb.h"
#include <stdlib.h>
#include <time.h>

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
};

// Process pending HTTP requests
void process_pending_requests(void);

// Start a non-blocking HTTP request
struct pending_request* start_http_request(struct mg_connection* client_conn, const char* url);

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
