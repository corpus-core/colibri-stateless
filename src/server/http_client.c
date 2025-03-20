#include "http_client.h"
#include <stdio.h>
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

  // Free resources
  if (req->remote_conn) {
    mg_close_connection(req->remote_conn);
  }
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

// Start a non-blocking HTTP request
struct pending_request* start_http_request(struct mg_connection* client_conn, const char* url) {
  char scheme[16];
  char host[256];
  int  port;
  char path[1024];

  if (!parse_url(url, scheme, sizeof(scheme), host, sizeof(host), &port, path, sizeof(path))) {
    mg_printf(client_conn,
              "HTTP/1.1 400 Bad Request\r\n"
              "Content-Type: text/plain\r\n"
              "Connection: close\r\n\r\n"
              "Invalid URL format: %s",
              url);
    return NULL;
  }

  int use_ssl = (strcmp(scheme, "https") == 0) ? 1 : 0;

  // Create request structure
  struct pending_request* req = (struct pending_request*) calloc(1, sizeof(struct pending_request));
  if (!req) {
    mg_printf(client_conn,
              "HTTP/1.1 500 Internal Server Error\r\n"
              "Content-Type: text/plain\r\n"
              "Connection: close\r\n\r\n"
              "Memory allocation failed");
    return NULL;
  }

  // Initialize request
  req->client_conn      = client_conn;
  req->url              = strdup(url);
  req->timestamp        = time(NULL);
  req->buffer_size      = 16384; // Larger initial buffer
  req->buffer           = (char*) malloc(req->buffer_size);
  req->buffer_len       = 0;
  req->request_sent     = 0;
  req->headers_parsed   = 0;
  req->content_length   = -1;
  req->debug_read_count = 0;

  if (!req->buffer || !req->url) {
    remove_pending_request(req);
    mg_printf(client_conn,
              "HTTP/1.1 500 Internal Server Error\r\n"
              "Content-Type: text/plain\r\n"
              "Connection: close\r\n\r\n"
              "Memory allocation failed");
    return NULL;
  }

  // Connect to remote server
  char error_buffer[256];
  printf("Connecting to %s:%d using SSL: %d\n", host, port, use_ssl);
  req->remote_conn = mg_connect_client(host, port, use_ssl, error_buffer, sizeof(error_buffer));

  if (!req->remote_conn) {
    printf("Error connecting to %s: %s\n", url, error_buffer);
    mg_printf(client_conn,
              "HTTP/1.1 502 Bad Gateway\r\n"
              "Content-Type: text/plain\r\n"
              "Connection: close\r\n\r\n"
              "Error connecting to remote server: %s",
              error_buffer);
    remove_pending_request(req);
    return NULL;
  }

  printf("Connected to %s:%d%s (SSL: %d)\n", host, port, path, use_ssl);

  // Send HTTP request - using curl-like headers to mimic curl request
  char request[2048];
  snprintf(request, sizeof(request),
           "GET %s HTTP/1.1\r\n"
           "Host: %s\r\n"
           "User-Agent: curl/8.7.1\r\n"
           "Accept: */*\r\n"
           "Connection: close\r\n\r\n",
           path, host);

  printf("Sending request:\n%s\n", request);
  mg_write(req->remote_conn, request, strlen(request));

  // Add to pending requests list
  add_pending_request(req);

  return req;
}

// Find header value in a HTTP response
char* find_header(const char* buffer, const char* header_name) {
  const char* header = strstr(buffer, header_name);
  if (!header) {
    return NULL;
  }

  // Skip header name and colon
  header += strlen(header_name);
  while (*header == ' ' || *header == ':') {
    header++;
  }

  // Find end of line
  const char* end = strstr(header, "\r\n");
  if (!end) {
    return NULL;
  }

  // Copy value
  size_t len   = end - header;
  char*  value = (char*) malloc(len + 1);
  if (!value) {
    return NULL;
  }

  strncpy(value, header, len);
  value[len] = '\0';

  return value;
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
  time_t                  now  = time(NULL);

  while (req) {
    // Timeout check (60 seconds)
    if (now - req->timestamp > 60) {
      struct pending_request* to_remove = req;
      if (prev) {
        prev->next = req->next;
        req        = req->next;
      }
      else {
        pending_requests = req->next;
        req              = pending_requests;
      }

      printf("Request timed out after 60 seconds: %s\n", to_remove->url);
      mg_printf(to_remove->client_conn,
                "HTTP/1.1 504 Gateway Timeout\r\n"
                "Content-Type: text/plain\r\n"
                "Connection: close\r\n\r\n"
                "Request timed out after 60 seconds");

      remove_pending_request(to_remove);
      continue;
    }

    // Check for data from remote server
    if (req->remote_conn) {
      // Read available data
      char buffer[4096];
      int  bytes_read = mg_read(req->remote_conn, buffer, sizeof(buffer) - 1);

      if (bytes_read > 0) {
        req->debug_read_count++;
        printf("Read %d bytes from remote server (read #%d)\n",
               bytes_read, req->debug_read_count);

        // Ensure buffer has enough space
        if (req->buffer_len + bytes_read + 1 > req->buffer_size) {
          size_t new_size   = req->buffer_size * 2;
          char*  new_buffer = (char*) realloc(req->buffer, new_size);
          if (!new_buffer) {
            printf("Failed to resize buffer to %zu bytes\n", new_size);
            mg_printf(req->client_conn,
                      "HTTP/1.1 500 Internal Server Error\r\n"
                      "Content-Type: text/plain\r\n"
                      "Connection: close\r\n\r\n"
                      "Memory allocation failed");

            struct pending_request* to_remove = req;
            if (prev) {
              prev->next = req->next;
              req        = req->next;
            }
            else {
              pending_requests = req->next;
              req              = pending_requests;
            }
            remove_pending_request(to_remove);
            continue;
          }
          req->buffer      = new_buffer;
          req->buffer_size = new_size;
          printf("Resized buffer to %zu bytes\n", new_size);
        }

        // Append data to buffer
        memcpy(req->buffer + req->buffer_len, buffer, bytes_read);
        req->buffer_len += bytes_read;
        req->buffer[req->buffer_len] = '\0';

        // If headers not parsed yet, try parsing now
        if (!req->headers_parsed) {
          const char* body_start = find_headers_end(req->buffer, req->buffer_len);
          if (body_start) {
            req->headers_parsed = 1;

            // Extract content length if present
            char* content_length_str = find_header(req->buffer, "Content-Length");
            if (content_length_str) {
              req->content_length = atoi(content_length_str);
              free(content_length_str);
              printf("Content-Length: %d bytes\n", req->content_length);
            }
          }
        }

        // Reset timeout
        req->timestamp = now;
      }
      else if (bytes_read == 0) {
        // Connection closed, send response to client
        printf("Remote connection closed, sending %zu bytes to client\n", req->buffer_len);

        // Check if we have any data to send
        if (req->buffer_len > 0) {
          print_buffer_preview(req->buffer, req->buffer_len);

          // If we parsed headers, we need to forward the response correctly
          if (req->headers_parsed) {
            // Just forward the entire response, including headers
            mg_write(req->client_conn, req->buffer, req->buffer_len);
          }
          else {
            // If we never got headers, construct our own
            mg_printf(req->client_conn,
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: %zu\r\n"
                      "Connection: close\r\n\r\n",
                      req->buffer_len);
            mg_write(req->client_conn, req->buffer, req->buffer_len);
          }
        }
        else {
          // No data received from server
          printf("Warning: No data received from server before connection closed\n");
          mg_printf(req->client_conn,
                    "HTTP/1.1 502 Bad Gateway\r\n"
                    "Content-Type: text/plain\r\n"
                    "Connection: close\r\n\r\n"
                    "No data received from remote server");
        }

        // Remove from list
        struct pending_request* to_remove = req;
        if (prev) {
          prev->next = req->next;
          req        = req->next;
        }
        else {
          pending_requests = req->next;
          req              = pending_requests;
        }
        remove_pending_request(to_remove);
        continue;
      }
      else if (bytes_read < 0) {
        // Error reading from server
        printf("Error reading from server: %d\n", bytes_read);
        mg_printf(req->client_conn,
                  "HTTP/1.1 502 Bad Gateway\r\n"
                  "Content-Type: text/plain\r\n"
                  "Connection: close\r\n\r\n"
                  "Error reading from remote server");

        // Remove from list
        struct pending_request* to_remove = req;
        if (prev) {
          prev->next = req->next;
          req        = req->next;
        }
        else {
          pending_requests = req->next;
          req              = pending_requests;
        }
        remove_pending_request(to_remove);
        continue;
      }
    }

    // Move to next request
    prev = req;
    req  = req->next;
  }
}