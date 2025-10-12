/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#define _GNU_SOURCE
#include "cache.h"
#include "server.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

// Platform-specific includes
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#define UV_TRY(cmd, msg, catch)                                  \
  do {                                                           \
    int r = cmd;                                                 \
    if (r != 0) {                                                \
      fprintf(stderr, ":: error %s: %s\n", msg, uv_strerror(r)); \
      catch;                                                     \
    }                                                            \
  } while (0)
// Connection structure
typedef struct mc_conn_s {
  mc_t*    client;
  void*    data;
  uv_tcp_t tcp;
  bool     in_use;
  bool     reconnecting; // Flag to indicate if the connection is in the process of reconnecting
} mc_conn_t;

// Define the operation types
typedef enum mc_op_type {
  QUEUE_OP_GET = 1,
  QUEUE_OP_SET = 2
} mc_op_type_t;

// Enum for request types (distinct from queue op types)
typedef enum req_type {
  REQ_GET = 10,
  REQ_SET = 11
} req_type_e;

// Simple queued operation structure - NO callback list, just basic fields
typedef struct mc_queued_op_s {
  mc_op_type_t type;
  union {
    struct {
      char*       key;
      size_t      keylen;
      void*       data;
      memcache_cb cb;
    } get;
    struct {
      char*    key;
      size_t   keylen;
      char*    value;
      size_t   value_len;
      uint32_t ttl;
    } set;
  } op;
  struct mc_queued_op_s* next;
} mc_queued_op_t;

// Memcached client structure
typedef struct mc_s {
  uv_loop_t*         loop;
  mc_conn_t*         connections;
  unsigned int       size;
  unsigned int       connected;
  unsigned int       connecting;
  unsigned int       available;
  struct sockaddr_in server_addr;
  mc_queued_op_t*    queue_head;     // Operation queue head
  mc_queued_op_t*    queue_tail;     // Operation queue tail
  unsigned int       queue_size;     // Current queue size
  unsigned int       max_queue_size; // Maximum queue size (limit)
} mc_t;

// Request structure for set operations (Moved earlier)
typedef struct {
  req_type_e  type; // Type identifier (must be first)
  mc_t*       client;
  mc_conn_t*  connection;
  uv_buf_t    msg[3];
  bool        has_been_freed; // Flag to prevent double-free
  uv_write_t* write_req;      // Pointer to the write request struct
} mc_set_req_t;

// Request structure for get operations (Moved earlier)
typedef struct {
  req_type_e  type; // Type identifier (must be first)
  mc_t*       client;
  void*       data;
  memcache_cb cb;
  mc_conn_t*  connection;
  char*       key; // key to get
  size_t      keylen;
  uv_buf_t    msg[3];
  buffer_t    buffer;         // data received
  bool        has_been_freed; // Flag to prevent double-free
  uv_write_t* write_req;      // Pointer to the write request struct
} mc_get_req_t;

// Forward declarations for static free functions
static void req_get_free(mc_get_req_t* req);
static void req_set_free(mc_set_req_t* req);
// Forward declaration for connection release function
static void mc_release_connection(mc_conn_t* connection);

// Generic free function for request data
static void req_free(void* req_void) {
  if (!req_void) return;

  // Check type using the first member
  req_type_e type = *((req_type_e*) req_void);

  if (type == REQ_GET) {
    mc_get_req_t* req = (mc_get_req_t*) req_void;
    req_get_free(req); // req_get_free already checks has_been_freed
  }
  else if (type == REQ_SET) {
    mc_set_req_t* req = (mc_set_req_t*) req_void;
    req_set_free(req); // req_set_free already checks has_been_freed
  }
  else {
    fprintf(stderr, "Error: Unknown request type %d in req_free\n", type);
    // Avoid fallback free as it's unsafe without knowing the type
  }
}

// uv_close callback for memcached connections
static void on_conn_close(uv_handle_t* handle) {
  mc_conn_t* connection = (mc_conn_t*) handle->data;
  // --- START CHANGE ---
  // Comment out debug print
  // fprintf(stderr, "DEBUG: on_conn_close called for conn %p (current data: %p)\n", connection, connection ? connection->data : NULL);
  // --- END CHANGE ---
  // Note: We don't free the connection struct itself here,
  // that happens in memcache_free after the loop.
}

// memcache connecting
static void on_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  if (!handle || !buf) {
    fprintf(stderr, "Error: on_alloc called with invalid parameters\n");
    if (buf) {
      buf->base = NULL;
      buf->len  = 0;
    }
    return;
  }

  // Cap the allocation size to prevent excessive memory usage
  size_t safe_size = suggested_size;
  if (safe_size > 64 * 1024) {
    safe_size = 64 * 1024; // Limit to 64KB
  }

  buf->base = (char*) safe_malloc(safe_size);
  if (!buf->base) {
    fprintf(stderr, "Error: Failed to allocate memory in on_alloc\n");
    buf->len = 0;
    return;
  }

  buf->len = safe_size;
}
static void on_connection(uv_connect_t* req, int status) {
  mc_conn_t* connection = (mc_conn_t*) req->data;
  connection->tcp.data  = connection;
  mc_t* client          = connection->client;
  safe_free(req);

  client->connecting--;
  connection->reconnecting = false;

  if (status == 0) {
    client->connected++;
    client->available++;

    if (client->connected == client->size)
      fprintf(stderr, ":: connected all connections to memcached server\n");
  }
  else {
    fprintf(stderr, ":: error connecting to %s:%d status: %d\n", http_server.memcached_host, http_server.memcached_port, status);
  }
}

// Create a new memcached client
mc_t* memcache_new(unsigned int pool_size, const char* host, int port) {
  struct addrinfo hints = {0};
  hints.ai_family       = AF_INET;     // IPv4
  hints.ai_socktype     = SOCK_STREAM; // TCP

  // resolve address
  struct addrinfo* result;
  int              r = getaddrinfo(host, NULL, &hints, &result);
  if (r != 0) {
    fprintf(stderr, "Failed to resolve hostname %s: %s\n", host, gai_strerror(r));
    return NULL;
  }
  struct sockaddr_in addr;
  memcpy(&addr, result->ai_addr, sizeof(struct sockaddr_in));
  addr.sin_port = htons(port);
  freeaddrinfo(result);

  // create client
  mc_t* client = (mc_t*) safe_calloc(1, sizeof(mc_t));
  if (!client) return NULL;

  client->loop           = uv_default_loop();
  client->size           = pool_size;
  client->server_addr    = addr;
  client->connecting     = pool_size;
  client->connections    = (mc_conn_t*) safe_calloc(pool_size, sizeof(mc_conn_t));
  client->queue_head     = NULL;
  client->queue_tail     = NULL;
  client->queue_size     = 0;
  client->max_queue_size = pool_size * 10; // Allow 10x the pool size for queued operations

  if (!client->connections) {
    safe_free(client);
    return NULL;
  }

  for (unsigned int i = 0; i < pool_size; i++) {
    mc_conn_t* connection = client->connections + i;
    UV_TRY(uv_tcp_init(client->loop, &connection->tcp), "error initializing tcp", continue);
    connection->client   = client;
    connection->tcp.data = connection;
    uv_connect_t* req    = (uv_connect_t*) safe_calloc(1, sizeof(uv_connect_t));
    req->data            = connection;
    UV_TRY(uv_tcp_connect(req, &connection->tcp, (const struct sockaddr*) &addr, on_connection),
           "error connecting to memcached",
           safe_free(req));
  }

  return client;
}

// Function to clean up the operation queue - simplified version
static void mc_cleanup_queue(mc_t* client) {
  if (!client) return;

  mc_queued_op_t* current = client->queue_head;
  while (current) {
    mc_queued_op_t* next = current->next;

    if (current->type == QUEUE_OP_GET) {
      safe_free(current->op.get.key);
      // Call callback with NULL to indicate failure
      if (current->op.get.cb) {
        current->op.get.cb(current->op.get.data, NULL, 0);
      }
    }
    else { // QUEUE_OP_SET
      safe_free(current->op.set.key);
      safe_free(current->op.set.value);
    }

    safe_free(current);
    current = next;
  }

  client->queue_head = NULL;
  client->queue_tail = NULL;
  client->queue_size = 0;
}

// Free the memcached client
void memcache_free(mc_t** client_p) {
  if (!client_p || !*client_p) return;

  mc_t* client = *client_p;

  // Clean up any queued operations
  mc_cleanup_queue(client);

  // Close connections (only if not already closing)
  // During shutdown, uv_walk may have already closed these handles
  for (unsigned int i = 0; i < client->size; i++) {
    if (!uv_is_closing((uv_handle_t*) &client->connections[i].tcp)) {
      uv_close((uv_handle_t*) &client->connections[i].tcp, on_conn_close);
    }
  }

  // IMPORTANT: Do NOT free client->connections or client here!
  // libuv still has internal references to these handles in the loop's handle queue.
  // Freeing them now would cause use-after-free when uv_loop_close() iterates the queue.
  // This is a controlled "leak" - the OS will reclaim memory when the process exits anyway.
  // A proper fix would require refcounting or async cleanup, but that's overkill for shutdown.

  // safe_free(client->connections);  // UNSAFE - libuv still has references
  // safe_free(client);                // UNSAFE - would invalidate all connection->client pointers
  *client_p = NULL; // Clear the pointer to prevent double-free attempts
}

static mc_conn_t* mc_get_connection(mc_t* client) {
  if (!client) {
    fprintf(stderr, "Error: mc_get_connection called with NULL client\n");
    return NULL;
  }

  // If no connections are available, log it
  if (client->available == 0) {
    fprintf(stderr, "Warning: No available connections in pool (size: %d, connected: %d)\n",
            client->size, client->connected);

    // Debug output for all connections
    for (unsigned int i = 0; i < client->size; i++) {
      fprintf(stderr, "Connection %d: in_use=%d, reconnecting=%d\n",
              i, client->connections[i].in_use, client->connections[i].reconnecting);
    }
    return NULL;
  }

  // Find an available connection
  for (unsigned int i = 0; i < client->size; i++) {
    mc_conn_t* connection = client->connections + i;

    // Skip connections that are in use or reconnecting
    if (connection->in_use || connection->reconnecting) {
      continue;
    }

    // Connection looks valid, mark it as in use
    client->available--;
    connection->in_use = true;
    return connection;
  }

  return NULL;
}

// Helper function to find if a GET operation with the same key exists in the queue
static mc_queued_op_t* mc_find_queued_get(mc_t* client, const char* key, size_t keylen) {
  if (!client || !client->queue_head || !key || keylen == 0) return NULL;

  mc_queued_op_t* op = client->queue_head;
  while (op != NULL) {
    if (op->type == QUEUE_OP_GET &&
        op->op.get.keylen == keylen &&
        memcmp(op->op.get.key, key, keylen) == 0) {
      return op;
    }
    op = op->next;
  }
  return NULL;
}

// Function to add an operation to the queue
static int mc_queue_operation(mc_t* client, mc_queued_op_t* op) {
  if (!client || !op) return -1;

  // Check if queue is full (safety limit to prevent memory exhaustion)
  if (client->queue_size >= client->max_queue_size) {
    fprintf(stderr, "Memcached operation queue is full (%d operations waiting)\n",
            client->queue_size);
    return -1;
  }

  // Add to queue
  op->next = NULL;
  if (client->queue_tail) {
    client->queue_tail->next = op;
  }
  else {
    client->queue_head = op;
  }
  client->queue_tail = op;
  client->queue_size++;

  return 0;
}

// Function to process the next operation from the queue
static void mc_process_queue(mc_t* client) {
  if (!client || !client->queue_head || client->available == 0) return;

  mc_queued_op_t* op = client->queue_head;
  client->queue_head = op->next;
  if (!client->queue_head) {
    client->queue_tail = NULL;
  }
  client->queue_size--;

  int result = 0;
  if (op->type == QUEUE_OP_GET) {
    result = memcache_get(client, op->op.get.key, op->op.get.keylen, op->op.get.data, op->op.get.cb);
    safe_free(op->op.get.key);
  }
  else { // QUEUE_OP_SET
    result = memcache_set(client, op->op.set.key, op->op.set.keylen,
                          op->op.set.value, op->op.set.value_len,
                          op->op.set.ttl);
    safe_free(op->op.set.key);
    safe_free(op->op.set.value);
  }

  safe_free(op);

  // If the operation processing freed up a connection and we have more operations,
  // process the next one
  if (result == 0 && client->queue_head && client->available > 0) {
    mc_process_queue(client);
  }
}

/// -------- SET ---------

static void req_set_free(mc_set_req_t* req) {
  if (!req) {
    return; // Prevent double-free
  }
  // --- START CHANGE ---
  // Comment out debug print
  // fprintf(stderr, "DEBUG: req_set_free freeing req %p\n", req);
  // --- END CHANGE ---
  safe_free(req->msg[0].base);
  safe_free(req->msg[1].base);
  // Free the associated write request if it hasn't been freed yet
  if (req->write_req) {
    safe_free(req->write_req);
    req->write_req = NULL; // Avoid double free if somehow called again
  }
  safe_free(req);
}

static void on_set_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  // Safety check for stream
  if (!stream) {
    fprintf(stderr, "Error: on_set_read called with null stream\n");
    if (buf && buf->base) safe_free(buf->base);
    return;
  }

  mc_conn_t* connection = (mc_conn_t*) stream->data;
  // Check if connection is valid
  if (!connection) {
    fprintf(stderr, "Error: Invalid connection in on_set_read\n");
    if (buf && buf->base) safe_free(buf->base);
    return;
  }

  mc_set_req_t* req_data = (mc_set_req_t*) connection->data;
  // Check if request data is valid
  if (!req_data || req_data->has_been_freed) {
    fprintf(stderr, "Error: Invalid or already freed request data in on_set_read\n");
    if (buf && buf->base) safe_free(buf->base);
    return;
  }

  // Stop reading first to prevent race conditions
  uv_read_stop(stream);

  // Check for connection errors
  if (nread < 0 && nread != UV_EOF) {
    fprintf(stderr, "Connection error in set operation: %s\n", uv_strerror(nread));
  }

  bool success = false;
  if (nread > 0 && buf && buf->base) {
    char* response  = buf->base;
    response[nread] = '\0';

    // Check if the set was successful
    if (strncmp(response, "STORED", 6) == 0) {
      success = true;
    }
    else {
      fprintf(stderr, "Memcached SET error: %s\n", response);
    }
  }

  // Mark the request as being handled (important for req_free)
  // req_data->has_been_freed = true; // Flag removed

  // Release connection before freeing request - REMOVED
  // mc_release_connection(connection);

  // Free the request data itself - REMOVED (handled by on_conn_close)
  // req_set_free(req_data);
}

// NEW: Callback to simply discard the read result for SET operations
static void on_set_read_discard(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  // Safety check for stream
  if (!stream) {
    fprintf(stderr, "Error: on_set_read_discard called with null stream\n");
    if (buf && buf->base) safe_free(buf->base);
    return;
  }

  mc_conn_t* connection = (mc_conn_t*) stream->data;
  // Check if connection is valid
  if (!connection) {
    fprintf(stderr, "Error: Invalid connection in on_set_read_discard\n");
    if (buf && buf->base) safe_free(buf->base);
    return;
  }

  // Get req_data BEFORE releasing connection
  mc_set_req_t* req_data = (mc_set_req_t*) connection->data;

  // Stop reading, we got the response (or an error)
  uv_read_stop(stream);

  // Log errors but otherwise ignore the response content
  if (nread < 0 && nread != UV_EOF) {
    fprintf(stderr, "Connection error discarding set response: %s\n", uv_strerror(nread));
    // Don't free req_data or release connection here, let on_conn_close handle it
    // Release the connection even on error
    mc_release_connection(connection); // This sets connection->data = NULL

    // Now free the request data if it was valid
    if (req_data) {
      req_set_free(req_data);
    }
  }
  // We don't care about the actual response ("STORED", "ERROR", etc.)

  // Free the buffer provided by on_alloc
  if (buf && buf->base) {
    safe_free(buf->base);
  }

  // Release the connection back to the pool (if not already released due to error)
  if (nread >= 0 || nread == UV_EOF) { // Only if no error previously handled release/free
    mc_release_connection(connection); // This sets connection->data = NULL

    // Now free the request data
    if (req_data) { // Check if req_data was valid before freeing
      req_set_free(req_data);
    }
  }
}

static void on_set_write(uv_write_t* req, int status) {
  // Check if request is valid
  if (!req) {
    fprintf(stderr, "Error: on_set_write called with NULL request\n");
    return;
  }

  mc_set_req_t* req_data = (mc_set_req_t*) req->data;
  if (!req_data || req_data->has_been_freed) {
    fprintf(stderr, "Error: Invalid or already freed request data in on_set_write\n");
    safe_free(req); // Free the write request struct itself
    return;
  }

  // Nullify the pointer in req_data before freeing req locally
  req_data->write_req = NULL;

  // Store connection reference for safety
  mc_conn_t* connection = req_data->connection;
  if (!connection) {
    fprintf(stderr, "Error: NULL connection in on_set_write\n");

    // Mark as freed - REMOVED
    // req_data->has_been_freed = true;

    // Set connection data to NULL before releasing/freeing - REMOVED
    // connection->data = NULL;

    // Release connection and free resources - ADJUSTED
    // mc_release_connection(connection); // DO NOT release potentially bad connection
    // req_set_free(req_data); // DO NOT free req_data, let on_conn_close handle it
    return; // Return added as we don't proceed to read
  }

  safe_free(req); // Free the write request struct itself

  if (status != 0) {
    fprintf(stderr, "Error writing SET to memcached: %s\n", uv_strerror(status));
    // DO NOT free req_data (on_conn_close handles it)
    // DO NOT touch connection (assume it's bad, let uv_close handle it)
    return; // Don't proceed to read
  }

  // START READ WITH NEW DISCARD CALLBACK
  int r = uv_read_start((uv_stream_t*) &connection->tcp, on_alloc, on_set_read_discard);
  if (r != 0) {
    fprintf(stderr, "Error starting read after SET write: %s\n", uv_strerror(r));
    // DO NOT free req_data (on_conn_close handles it)
    // DO NOT touch connection (assume it's bad, let uv_close handle it)
  }
}

// Set value in memcached
int memcache_set(mc_t* client, char* key, size_t keylen, char* value, size_t value_len,
                 uint32_t ttl) {
  if (!client || !key || keylen == 0 || !value) return -1;

  mc_conn_t* connection = mc_get_connection(client);
  if (!connection) {
    // No connections available, queue the operation
    mc_queued_op_t* op = (mc_queued_op_t*) safe_calloc(1, sizeof(mc_queued_op_t));
    if (!op) {
      return -1;
    }

    op->type       = QUEUE_OP_SET;
    op->op.set.key = strndup(key, keylen);
    if (!op->op.set.key) {
      safe_free(op);
      return -1;
    }
    op->op.set.keylen = keylen;
    op->op.set.value  = strndup(value, value_len);
    if (!op->op.set.value) {
      safe_free(op->op.set.key);
      safe_free(op);
      return -1;
    }
    op->op.set.value_len = value_len;
    op->op.set.ttl       = ttl;

    // Try to add to queue
    int result = mc_queue_operation(client, op);
    if (result != 0) {
      // Queue is full or other error, clean up and fail
      safe_free(op->op.set.key);
      safe_free(op->op.set.value);
      safe_free(op);
      return -1;
    }

    return 0;
  }

  // Use existing connection
  mc_set_req_t* req_data = (mc_set_req_t*) safe_calloc(1, sizeof(mc_set_req_t));
  if (!req_data) {
    if (connection) {
      connection->in_use = false; // Make connection available
      mc_process_queue(client);   // Try to process queue
    }
    return -1;
  }

  req_data->client     = client;
  req_data->connection = connection;
  req_data->type       = REQ_SET;

  // --- Allocation Path 1: command buffer ---
  buffer_t command_buf  = {0};
  req_data->msg[0].base = bprintf(&command_buf, "set %s 0 %d %d\r\n", key, (uint32_t) ttl, (uint32_t) value_len);
  // Assuming bprintf allocates; need to handle its failure if it can fail
  // For now, assume it succeeds or crashes

  // --- Allocation Path 2: value buffer ---
  req_data->msg[1].base = (char*) bytes_dup(bytes(value, value_len)).data;
  if (!req_data->msg[1].base) {       // Check allocation success for value dup
    safe_free(req_data->msg[0].base); // Free command buffer
    safe_free(req_data);              // Free req_data struct
    if (connection) {
      connection->in_use = false; // Make connection available
      mc_process_queue(client);   // Try to process queue
    }
    return -1;
  }
  req_data->msg[0].len  = command_buf.data.len;
  req_data->msg[1].len  = value_len;
  req_data->msg[2].base = "\r\n";
  req_data->msg[2].len  = 2;
  req_data->write_req   = NULL;

  // Associate req_data with connection *AFTER* most allocations
  connection->data = req_data;
  // --- START CHANGE ---
  // Comment out debug print
  // fprintf(stderr, "DEBUG: memcache_set assigned req %p to conn %p\n", req_data, connection);
  // --- END CHANGE ---

  // --- Allocation Path 3: write request struct ---
  uv_write_t* req = (uv_write_t*) safe_calloc(1, sizeof(uv_write_t));
  if (!req) {
    req_set_free(req_data); // Free buffers inside req_data
    if (connection) {
      // req_data is already freed internally by req_set_free
      connection->data   = NULL;  // Detach freed data
      connection->in_use = false; // Make connection available
      mc_process_queue(client);   // Try to process queue
    }
    // req_data struct itself is freed by req_set_free
    return -1;
  }
  req_data->write_req = req;      // Store pointer to write request
  req->data           = req_data; // Link req back to req_data

  // --- uv_write Path ---
  int r = uv_write(req, (uv_stream_t*) &connection->tcp, req_data->msg, 3, on_set_write);
  if (r != 0) {
    fprintf(stderr, "Error uv_write in memcache_set: %s\n", uv_strerror(r));
    req_data->write_req = NULL; // Nullify pointer in req_data
    safe_free(req);             // Free the uv_write_t struct
    // DO NOT free req_data (on_conn_close handles it)
    // DO NOT touch connection (assume it's bad)
    return -1;
  }

  return 0;
}

/// -------- GET ---------

static void req_get_free(mc_get_req_t* req) {
  if (!req) {
    return; // Prevent double-free
  }
  // --- START CHANGE ---
  // Comment out debug print
  // fprintf(stderr, "DEBUG: req_get_free freeing req %p\n", req);
  // --- END CHANGE ---
  safe_free(req->key);
  buffer_free(&req->buffer);
  // Free the associated write request if it hasn't been freed yet
  if (req->write_req) {
    safe_free(req->write_req);
    req->write_req = NULL; // Avoid double free
  }
  safe_free(req);
}

static void on_get_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  // Safety check for stream
  if (!stream) {
    fprintf(stderr, "Error: on_get_read called with null stream\n");
    if (buf && buf->base) safe_free(buf->base);
    return;
  }

  mc_conn_t* connection = (mc_conn_t*) stream->data;
  // Check if connection is valid
  if (!connection) {
    fprintf(stderr, "Error: Invalid connection in on_get_read\n");
    if (buf && buf->base) safe_free(buf->base);
    return;
  }

  // Get req_data BEFORE potential release
  mc_get_req_t* req_data = (mc_get_req_t*) connection->data;

  // Check if request data is valid
  if (!req_data /* Removed: || req_data->has_been_freed */) {
    fprintf(stderr, "Error: Invalid request data in on_get_read\n");
    if (buf && buf->base) safe_free(buf->base);
    uv_read_stop(stream); // Stop reading if request is bad
    // Don't release connection, let on_conn_close handle it if needed
    // Release connection even if request data was bad? Maybe not safe. Log and return.
    // mc_release_connection(connection); // Avoid releasing if state is unknown
    return;
  }

  bool    done   = false;
  bytes_t result = NULL_BYTES;

  // Check for connection errors
  if (nread < 0 && nread != UV_EOF) {
    fprintf(stderr, "Connection error in get operation: %s\n", uv_strerror(nread));
    done = true;
    uv_read_stop(stream); // Stop reading on error
  }
  else if (nread == UV_EOF) {
    // Consider EOF without proper END protocol message an error/incomplete response
    fprintf(stderr, "Warning: EOF received during get operation before END\n");
    done = true;
    uv_read_stop(stream); // Stop reading on EOF
  }
  else if (nread > 0 && buf && buf->base) {
    // Only append data if we have valid buffer and positive read length
    buffer_append(&req_data->buffer, bytes(buf->base, nread));
  }

  // Free the read buffer now, we've appended its content or handled error
  if (buf && buf->base) {
    safe_free(buf->base);
  }

  // Only proceed with parsing if we have data and aren't already done due to error/EOF
  if (!done && req_data->buffer.data.len > 0) {
    char* response   = (char*) req_data->buffer.data.data;
    char* end_header = NULL;

    // Find end of header
    for (size_t i = 0; i + 1 < req_data->buffer.data.len; i++) { // Use size_t for index
      if (response[i] == '\r' && response[i + 1] == '\n') {
        end_header = &response[i];
        break;
      }
    }

    // Check for "END" response (cache miss)
    // Need to check for "\r\n" after END for robustness
    char* end_marker = (char*) memmem(response, req_data->buffer.data.len, "END\r\n", 5);
    if (end_marker == response) { // END is at the very beginning
      done = true;
      uv_read_stop(stream); // Stop reading if protocol END received
    }
    else if (end_header) { // Found a potential header line
      size_t value_len = 0;
      char   key[256]  = {0}; // Ensure buffer is large enough or dynamically size
      char   flags[32] = {0};
      // Optional CAS value
      // char   cas[32]   = {0};

      // Create a null-terminated copy of the header for sscanf
      size_t header_len = end_header - response;
      char*  header     = (char*) safe_malloc(header_len + 1);
      if (header) {
        memcpy(header, response, header_len);
        header[header_len] = '\0';

        // Parse header: VALUE <key> <flags> <bytes> [<cas unique>]\r\n
        // sscanf might be fragile; consider manual parsing for robustness
        // We primarily need the <bytes> field (value_len)
        int scan_res = sscanf(header, "VALUE %255s %31s %zu", key, flags, &value_len);

        if (scan_res >= 3) {                                              // Successfully parsed key, flags, and value_len
          size_t header_data_offset = header_len + 2;                     // Offset of data after header + \r\n
          size_t expected_total_len = header_data_offset + value_len + 7; // + \r\nEND\r\n (worst case)

          // Check if we have received the complete value data AND the trailing \r\nEND\r\n
          char* end_of_value = (char*) req_data->buffer.data.data + header_data_offset + value_len;
          if (req_data->buffer.data.len >= header_data_offset + value_len + 2 && // Check for at least \r\n after value
              memcmp(end_of_value, "\r\n", 2) == 0) {

            // Check for END\r\n after the value's \r\n
            if (req_data->buffer.data.len >= header_data_offset + value_len + 7 &&
                memcmp(end_of_value + 2, "END\r\n", 5) == 0) {
              done   = true;
              result = bytes_slice(req_data->buffer.data, header_data_offset, value_len);
              uv_read_stop(stream); // Stop reading, we got the value and END
            }
            // Handle cases where just \r\n exists after value (maybe lenient servers?)
            // Or if we only have value + \r\n but not END yet - continue reading?
            // For now, require END\r\n
          }
          // If not enough data yet for value + \r\nEND\r\n, continue reading (done remains false)
        }
        else {
          fprintf(stderr, "Warning: Failed to parse memcached VALUE header: '%s'\n", header);
          // Treat as error? Or maybe just END not found yet? Let's treat as error for now.
          done = true;
          uv_read_stop(stream);
        }
        safe_free(header);
      }
      else {
        // Failed to allocate header copy - treat as error
        fprintf(stderr, "Error: Failed to allocate memory for header parsing\n");
        done = true;
        uv_read_stop(stream);
      }
    }
    // If no END and no valid VALUE header found yet, continue reading (done remains false)
  }

  if (done) {
    // Store callback and data locally BEFORE releasing connection/freeing req_data
    memcache_cb cb      = req_data ? req_data->cb : NULL;
    void*       cb_data = req_data ? req_data->data : NULL;

    // Release the connection back to the pool
    mc_release_connection(connection); // This sets connection->data = NULL

    // Call the user callback (if valid)
    if (cb) {
      cb(cb_data, (char*) result.data, result.len);
    }

    // Now free the request data
    if (req_data) { // Check if req_data was valid before freeing
      req_get_free(req_data);
    }
  }
}

static void on_get_write(uv_write_t* req, int status) {
  // Check if request is valid
  if (!req) {
    fprintf(stderr, "Error: on_get_write called with NULL request\n");
    return;
  }

  mc_get_req_t* req_data = (mc_get_req_t*) req->data;
  if (!req_data || req_data->has_been_freed) {
    fprintf(stderr, "Error: Invalid or already freed request data in on_get_write\n");
    safe_free(req); // Free the write request struct itself
    return;
  }

  // Nullify the pointer in req_data before freeing req locally
  req_data->write_req = NULL;

  // Store connection reference for safety
  mc_conn_t* connection = req_data->connection;
  if (!connection) {
    fprintf(stderr, "Error: NULL connection in on_get_write\n");

    // Mark as freed - REMOVED
    // req_data->has_been_freed = true;

    // Free resources
    req_get_free(req_data);
    safe_free(req); // Free the write request struct itself

    // Call callback with error after cleanup
    if (req_data->cb) {
      req_data->cb(req_data->data, NULL, 0);
    }
    return;
  }

  safe_free(req); // Free the write request struct itself

  if (status != 0) {
    fprintf(stderr, "Error writing GET request to memcached: %s\n", uv_strerror(status));

    // Make a local copy of callback and data
    memcache_cb cb      = req_data->cb;
    void*       cb_data = req_data->data;

    // Mark as freed - REMOVED
    // req_data->has_been_freed = true;

    // Set connection data to NULL before releasing/freeing - REMOVED
    // connection->data = NULL;

    // Release connection and free resources - ADJUSTED
    // mc_release_connection(connection); // DO NOT release potentially bad connection
    // req_get_free(req_data); // DO NOT free req_data, let on_conn_close handle it

    // Call callback with error after cleanup
    if (cb) {
      cb(cb_data, NULL, 0);
    }
    // Return, don't start read
    return;
  }

  int r = uv_read_start((uv_stream_t*) &connection->tcp, on_alloc, on_get_read);
  if (r != 0) {
    fprintf(stderr, "Error starting read after GET request: %s\n", uv_strerror(r));

    // Make a local copy of callback and data
    memcache_cb cb      = req_data->cb;
    void*       cb_data = req_data->data;

    // Mark as freed - REMOVED
    // req_data->has_been_freed = true;

    // Set connection data to NULL before releasing/freeing - REMOVED
    // connection->data = NULL;

    // Release connection and free resources - ADJUSTED
    // mc_release_connection(connection); // DO NOT release potentially bad connection
    // req_get_free(req_data); // DO NOT free req_data, let on_conn_close handle it

    // Call callback with error after cleanup
    if (cb) {
      cb(cb_data, NULL, 0);
    }
  }
}

// Get value from memcached
int memcache_get(mc_t* client, char* key, size_t keylen, void* data, memcache_cb cb) {
  if (!client) {
    if (cb) cb(data, NULL, 0);
    return 0;
  }
  if (!key || keylen == 0 || !cb) return -1;

  mc_conn_t* connection = mc_get_connection(client);
  if (!connection) {
    // No connections available, queue the operation
    mc_queued_op_t* op = (mc_queued_op_t*) safe_calloc(1, sizeof(mc_queued_op_t));
    if (!op) {
      cb(data, NULL, 0);
      return -1;
    }

    // Initialize the operation
    op->type       = QUEUE_OP_GET;
    op->op.get.key = strndup(key, keylen);
    if (!op->op.get.key) {
      safe_free(op);
      cb(data, NULL, 0);
      return -1;
    }
    op->op.get.keylen = keylen;
    op->op.get.data   = data;
    op->op.get.cb     = cb;

    // Try to add to queue
    int result = mc_queue_operation(client, op);
    if (result != 0) {
      // Queue is full or other error, clean up and fail
      safe_free(op->op.get.key);
      safe_free(op);
      cb(data, NULL, 0);
      return -1;
    }

    return 0;
  }

  // Rest of the function remains unchanged - existing implementation for when a connection is available
  mc_get_req_t* req_data = (mc_get_req_t*) safe_calloc(1, sizeof(mc_get_req_t));
  if (!req_data) {
    if (connection) {
      connection->in_use = false; // Make connection available
      mc_process_queue(client);   // Try to process queue
    }
    cb(data, NULL, 0);
    return -1;
  }

  req_data->client     = client;
  req_data->data       = data;
  req_data->cb         = cb;
  req_data->connection = connection;
  req_data->key        = strndup(key, keylen);
  if (!req_data->key) { // Check allocation success for key dup
    safe_free(req_data);
    if (connection) {
      connection->in_use = false; // Make connection available
      mc_process_queue(client);   // Try to process queue
    }
    cb(data, NULL, 0);
    return -1;
  }
  req_data->keylen      = keylen;
  req_data->type        = REQ_GET;
  req_data->msg[0].base = "get ";
  req_data->msg[0].len  = 4;
  req_data->msg[1].base = req_data->key;
  req_data->msg[1].len  = req_data->keylen;
  req_data->msg[2].base = "\r\n";
  req_data->msg[2].len  = 2;
  req_data->write_req   = NULL; // Initialize to NULL

  uv_write_t* req = (uv_write_t*) safe_calloc(1, sizeof(uv_write_t));
  if (!req) {
    req_get_free(req_data); // Frees key
    if (connection) {
      // req_data is already freed internally by req_get_free
      connection->data   = NULL;  // Detach freed data
      connection->in_use = false; // Make connection available
      mc_process_queue(client);   // Try to process queue
    }
    cb(data, NULL, 0);
    return -1;
  }
  req_data->write_req = req; // Store pointer to write request

  req->data        = req_data;
  connection->data = req_data;
  // --- START CHANGE ---
  // Comment out debug print
  // fprintf(stderr, "DEBUG: memcache_get assigned req %p to conn %p\n", req_data, connection);
  // --- END CHANGE ---

  int r = uv_write(req, (uv_stream_t*) &connection->tcp, req_data->msg, 3, on_get_write);
  if (r != 0) {
    fprintf(stderr, "Error uv_write in memcache_get: %s\n", uv_strerror(r));
    req_data->write_req = NULL;
    safe_free(req);
    // DO NOT free req_data (on_conn_close handles it)
    // DO NOT touch connection (assume it's bad)
    cb(data, NULL, 0); // Call user callback with error
    return -1;
  }

  return 0;
}

// Check and potentially process queued operations whenever a connection becomes available
static void mc_connection_available(mc_t* client) {
  // If we have queued operations and available connections, process them
  if (client->queue_head && client->available > 0) {
    mc_process_queue(client);
  }
}

// Call mc_connection_available when a connection becomes available
// This needs to be called in places where connections are freed/become available

// --- START NEW FUNCTION ---
// Helper function to release a connection back to the pool
static void mc_release_connection(mc_conn_t* connection) {
  if (!connection || !connection->client) {
    fprintf(stderr, "Error: mc_release_connection called with invalid connection/client\n");
    return;
  }
  mc_t* client = connection->client;

  // Only release if it was actually in use
  if (connection->in_use) {
    connection->in_use = false;
    client->available++;
    // --- START CHANGE ---
    // Comment out debug print
    // fprintf(stderr, "DEBUG: Releasing connection %p, available now %d\n", connection, client->available);
    // --- END CHANGE ---

    // Clear associated request data as it's being handled by the caller
    connection->data = NULL;

    // If there are queued operations, try to process one now
    if (client->queue_head) {
      mc_process_queue(client);
    }
  }
  else {
    fprintf(stderr, "DEBUG: mc_release_connection called on connection %p already released or never used?\n", connection);
    // Still clear data just in case
    connection->data = NULL;
  }
}
// --- END NEW FUNCTION ---
