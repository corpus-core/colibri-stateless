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
  fprintf(stderr, "DEBUG: on_conn_close called for conn %p (current data: %p)\n", connection, connection ? connection->data : NULL);
  if (connection && connection->data) {
    // Attempt to free the request data associated with the connection
    req_free(connection->data);
    connection->data = NULL; // Avoid double free attempts elsewhere
  }
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

  buf->base = (char*) malloc(safe_size);
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
  free(req);

  client->connecting--;
  connection->reconnecting = false;

  if (status == 0) {
    client->connected++;
    client->available++;

    if (client->connected == client->size)
      printf(":: connected all connections to memcached server\n");
  }
  else {
    printf(":: error connecting to %s:%d status: %d\n", http_server.memcached_host, http_server.memcached_port, status);
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
  mc_t* client = (mc_t*) calloc(1, sizeof(mc_t));
  if (!client) return NULL;

  client->loop           = uv_default_loop();
  client->size           = pool_size;
  client->server_addr    = addr;
  client->connecting     = pool_size;
  client->connections    = (mc_conn_t*) calloc(pool_size, sizeof(mc_conn_t));
  client->queue_head     = NULL;
  client->queue_tail     = NULL;
  client->queue_size     = 0;
  client->max_queue_size = pool_size * 10; // Allow 10x the pool size for queued operations

  if (!client->connections) {
    free(client);
    return NULL;
  }

  for (unsigned int i = 0; i < pool_size; i++) {
    mc_conn_t* connection = client->connections + i;
    UV_TRY(uv_tcp_init(client->loop, &connection->tcp), "error initializing tcp", continue);
    connection->client   = client;
    connection->tcp.data = connection;
    uv_connect_t* req    = (uv_connect_t*) calloc(1, sizeof(uv_connect_t));
    req->data            = connection;
    UV_TRY(uv_tcp_connect(req, &connection->tcp, (const struct sockaddr*) &addr, on_connection),
           "error connecting to memcached",
           free(req));
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
      free(current->op.get.key);
      // Call callback with NULL to indicate failure
      if (current->op.get.cb) {
        current->op.get.cb(current->op.get.data, NULL, 0);
      }
    }
    else { // QUEUE_OP_SET
      free(current->op.set.key);
      free(current->op.set.value);
    }

    free(current);
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

  // Close connections
  for (unsigned int i = 0; i < client->size; i++)
    uv_close((uv_handle_t*) &client->connections[i].tcp, on_conn_close);

  free(client->connections);
  free(client);
  *client_p = NULL;
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
    free(op->op.get.key);
  }
  else { // QUEUE_OP_SET
    result = memcache_set(client, op->op.set.key, op->op.set.keylen,
                          op->op.set.value, op->op.set.value_len,
                          op->op.set.ttl);
    free(op->op.set.key);
    free(op->op.set.value);
  }

  free(op);

  // If the operation processing freed up a connection and we have more operations,
  // process the next one
  if (result == 0 && client->queue_head && client->available > 0) {
    mc_process_queue(client);
  }
}

// Modify mc_release_connection to process queued operations
static void mc_release_connection(mc_conn_t* connection) {
  if (!connection) return;

  fprintf(stderr, "DEBUG: mc_release_connection called for conn %p (current data: %p)\n", connection, connection->data);

  // No-op if the connection is already marked as not in use
  if (!connection->in_use) {
    fprintf(stderr, "Warning: Attempt to release a connection that is not marked as in-use\n");
    return;
  }

  // Clear the connection's data pointer to prevent any callbacks from using it
  connection->data = NULL;

  // Mark the connection as not in use
  connection->in_use = false;

  // Update the client's available count
  mc_t* client = connection->client;
  if (client) {
    client->available++;

    // Process the next queued operation if any
    if (client->queue_head) {
      mc_process_queue(client);
    }
  }
}

/// -------- SET ---------

static void req_set_free(mc_set_req_t* req) {
  if (!req || req->has_been_freed) {
    return; // Prevent double-free
  }
  fprintf(stderr, "DEBUG: req_set_free freeing req %p\n", req);
  req->has_been_freed = true;
  free(req->msg[0].base);
  free(req->msg[1].base);
  // Free the associated write request if it hasn't been freed yet
  if (req->write_req) {
    free(req->write_req);
    req->write_req = NULL; // Avoid double free if somehow called again
  }
  free(req);
}

static void on_set_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  // Safety check for stream
  if (!stream) {
    fprintf(stderr, "Error: on_set_read called with null stream\n");
    if (buf && buf->base) free(buf->base);
    return;
  }

  mc_conn_t* connection = (mc_conn_t*) stream->data;
  // Check if connection is valid
  if (!connection) {
    fprintf(stderr, "Error: Invalid connection in on_set_read\n");
    if (buf && buf->base) free(buf->base);
    return;
  }

  mc_set_req_t* req_data = (mc_set_req_t*) connection->data;
  // Check if request data is valid
  if (!req_data || req_data->has_been_freed) {
    fprintf(stderr, "Error: Invalid or already freed request data in on_set_read\n");
    if (buf && buf->base) free(buf->base);
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

  // Set connection data to NULL before releasing/freeing
  connection->data = NULL;

  // Mark the request as being handled
  req_data->has_been_freed = true;

  // Release connection before freeing request to avoid race conditions
  mc_release_connection(connection);

  // Free the request data
  req_set_free(req_data);

  if (buf && buf->base) {
    free(buf->base);
  }
}

// NEW: Callback to simply discard the read result for SET operations
static void on_set_read_discard(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  // Safety check for stream
  if (!stream) {
    fprintf(stderr, "Error: on_set_read_discard called with null stream\n");
    if (buf && buf->base) free(buf->base);
    return;
  }

  mc_conn_t* connection = (mc_conn_t*) stream->data;
  // Check if connection is valid
  if (!connection) {
    fprintf(stderr, "Error: Invalid connection in on_set_read_discard\n");
    if (buf && buf->base) free(buf->base);
    return;
  }

  mc_set_req_t* req_data = (mc_set_req_t*) connection->data;
  // Check if request data is valid (although we won't use it much)
  if (!req_data || req_data->has_been_freed) {
    fprintf(stderr, "Error: Invalid or already freed request data in on_set_read_discard\n");
    if (buf && buf->base) free(buf->base);
    // Release connection even if req_data is bad, as we stopped reading
    mc_release_connection(connection);
    return;
  }

  // Stop reading, just in case (though likely already stopped by error/EOF)
  uv_read_stop(stream);

  // Log errors but otherwise ignore the response content
  if (nread < 0 && nread != UV_EOF) {
    fprintf(stderr, "Connection error discarding set response: %s\n", uv_strerror(nread));
  }
  // We don't care about the actual response ("STORED", "ERROR", etc.)

  // Free the buffer provided by on_alloc
  if (buf && buf->base) {
    free(buf->base);
  }

  // Set connection data to NULL before releasing/freeing
  connection->data = NULL;

  // Mark the request as being handled (important for req_free)
  req_data->has_been_freed = true;

  // Release connection before freeing request
  mc_release_connection(connection);

  // Free the request data itself
  req_set_free(req_data);
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
    free(req); // Free the write request struct itself
    return;
  }

  // Nullify the pointer in req_data before freeing req locally
  req_data->write_req = NULL;

  // Store connection reference for safety
  mc_conn_t* connection = req_data->connection;
  if (!connection) {
    fprintf(stderr, "Error: NULL connection in on_set_write\n");

    // Mark as freed
    req_data->has_been_freed = true;

    // Free resources
    req_set_free(req_data);
    free(req); // Free the write request struct itself
    return;
  }

  free(req); // Free the write request struct itself

  if (status != 0) {
    fprintf(stderr, "Error writing to memcached: %s\n", uv_strerror(status));

    // Mark as freed
    req_data->has_been_freed = true;

    // Set connection data to NULL before releasing/freeing
    connection->data = NULL;

    // Release connection and free resources
    mc_release_connection(connection);
    req_set_free(req_data);
    return; // Return added as we don't proceed to read
  }

  // START READ WITH NEW DISCARD CALLBACK
  int r = uv_read_start((uv_stream_t*) &connection->tcp, on_alloc, on_set_read_discard);
  if (r != 0) {
    fprintf(stderr, "Error starting read: %s\n", uv_strerror(r));

    // Mark as freed
    req_data->has_been_freed = true;

    // Set connection data to NULL before releasing/freeing
    connection->data = NULL;

    // Release connection and free resources
    mc_release_connection(connection);
    req_set_free(req_data);
  }
}

// Set value in memcached
int memcache_set(mc_t* client, char* key, size_t keylen, char* value, size_t value_len,
                 uint32_t ttl) {
  if (!client || !key || keylen == 0 || !value) return -1;

  mc_conn_t* connection = mc_get_connection(client);
  if (!connection) {
    // No connections available, queue the operation
    mc_queued_op_t* op = (mc_queued_op_t*) calloc(1, sizeof(mc_queued_op_t));
    if (!op) {
      return -1;
    }

    op->type       = QUEUE_OP_SET;
    op->op.set.key = strndup(key, keylen);
    if (!op->op.set.key) {
      free(op);
      return -1;
    }
    op->op.set.keylen = keylen;
    op->op.set.value  = strndup(value, value_len);
    if (!op->op.set.value) {
      free(op->op.set.key);
      free(op);
      return -1;
    }
    op->op.set.value_len = value_len;
    op->op.set.ttl       = ttl;

    // Try to add to queue
    int result = mc_queue_operation(client, op);
    if (result != 0) {
      // Queue is full or other error, clean up and fail
      free(op->op.set.key);
      free(op->op.set.value);
      free(op);
      return -1;
    }

    return 0;
  }

  // Use existing connection
  mc_set_req_t* req_data = (mc_set_req_t*) calloc(1, sizeof(mc_set_req_t));
  if (!req_data) {
    mc_release_connection(connection);
    return -1;
  }

  req_data->client      = client;
  req_data->connection  = connection;
  req_data->type        = REQ_SET;
  buffer_t command_buf  = {0};
  req_data->msg[0].base = bprintf(&command_buf, "set %s 0 %d %d\r\n", key, (uint32_t) ttl, (uint32_t) value_len);
  req_data->msg[0].len  = command_buf.data.len;
  req_data->msg[1].base = (char*) bytes_dup(bytes(value, value_len)).data;
  req_data->msg[1].len  = value_len;
  req_data->msg[2].base = "\r\n";
  req_data->msg[2].len  = 2;
  req_data->write_req   = NULL; // Initialize to NULL

  uv_write_t* req = (uv_write_t*) calloc(1, sizeof(uv_write_t));
  if (!req) {
    req_set_free(req_data); // Use req_set_free to clean up msg buffers
    mc_release_connection(connection);
    return -1;
  }
  req_data->write_req = req; // Store pointer to write request

  req->data        = req_data;
  connection->data = req_data;
  fprintf(stderr, "DEBUG: memcache_set assigned req %p to conn %p\n", req_data, connection);

  int r = uv_write(req, (uv_stream_t*) &connection->tcp, req_data->msg, 3, on_set_write);
  if (r != 0) {
    mc_release_connection(connection);
    req_set_free(req_data);
    return -1;
  }

  return 0;
}

/// -------- GET ---------

static void req_get_free(mc_get_req_t* req) {
  if (!req || req->has_been_freed) {
    return; // Prevent double-free
  }
  fprintf(stderr, "DEBUG: req_get_free freeing req %p\n", req);
  req->has_been_freed = true;
  free(req->key);
  buffer_free(&req->buffer);
  // Free the associated write request if it hasn't been freed yet
  if (req->write_req) {
    free(req->write_req);
    req->write_req = NULL; // Avoid double free
  }
  free(req);
}

static void on_get_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  // Safety check for stream
  if (!stream) {
    fprintf(stderr, "Error: on_get_read called with null stream\n");
    if (buf && buf->base) free(buf->base);
    return;
  }

  mc_conn_t* connection = (mc_conn_t*) stream->data;
  // Check if connection is valid
  if (!connection) {
    fprintf(stderr, "Error: Invalid connection in on_get_read\n");
    if (buf && buf->base) free(buf->base);
    return;
  }

  mc_get_req_t* req_data = (mc_get_req_t*) connection->data;
  // Check if request data is valid
  if (!req_data || req_data->has_been_freed) {
    fprintf(stderr, "Error: Invalid or already freed request data in on_get_read\n");
    if (buf && buf->base) free(buf->base);
    return;
  }

  bool    done   = false;
  bytes_t result = NULL_BYTES;

  // Stop reading first to prevent any race conditions
  uv_read_stop(stream);

  // Check for connection errors
  if (nread < 0 && nread != UV_EOF) {
    fprintf(stderr, "Connection error in get operation: %s\n", uv_strerror(nread));
    done = true;
  }
  else if (nread == UV_EOF) {
    done = true;
  }
  else if (nread > 0 && buf && buf->base) {
    // Only append data if we have valid buffer and positive read length
    buffer_append(&req_data->buffer, bytes(buf->base, nread));
  }

  if (buf && buf->base) {
    free(buf->base);
  }

  // Only proceed with parsing if we have data
  if (!done && req_data->buffer.data.len > 0) {
    char* response   = (char*) req_data->buffer.data.data;
    char* end_header = NULL;

    // Find end of header
    for (int i = 0; i + 1 < req_data->buffer.data.len; i++) {
      if (response[i] == '\r' && response[i + 1] == '\n') {
        end_header = &response[i];
        break;
      }
    }

    // Check for "END" response (cache miss)
    if (req_data->buffer.data.len >= 3 && strncmp(response, "END", 3) == 0) {
      done = true;
    }
    else if (end_header) {
      size_t value_len = 0;
      char   key[256]  = {0};
      char   flags[32] = {0};
      char   cas[32]   = {0};

      // Create a null-terminated copy of the header for sscanf
      size_t header_len = end_header - response;
      char*  header     = (char*) malloc(header_len + 1);
      if (header) {
        memcpy(header, response, header_len);
        header[header_len] = '\0';

        if (sscanf(header, "VALUE %s %s %zu %s", key, flags, &value_len, cas) >= 3) {
          size_t expected_buffer_len = header_len + value_len + 2;
          if (req_data->buffer.data.len >= expected_buffer_len) {
            done   = true;
            result = bytes_slice(req_data->buffer.data, header_len + 2, value_len);
          }
        }
        free(header);
      }
    }
  }

  if (done) {
    // Make a local copy of callback and data before releasing connection
    memcache_cb cb      = req_data->cb;
    void*       cb_data = req_data->data;
    // Set connection data to NULL before releasing/freeing
    connection->data = NULL;

    // Local copy of the result to ensure it's valid after we free req_data
    char*  result_data = NULL;
    size_t result_len  = 0;

    if (result.data && result.len > 0) {
      result_data = (char*) malloc(result.len);
      if (result_data) {
        memcpy(result_data, result.data, result.len);
        result_len = result.len;
      }
    }

    // Mark the request as being handled
    req_data->has_been_freed = true;

    // Release connection first to make it available for reuse
    mc_release_connection(connection);

    // Free the request data before calling the callback
    req_get_free(req_data);

    // Call callback with result
    if (cb && cb_data) {
      cb(cb_data, result_data, result_len);
    }

    // Free our local copy
    if (result_data) {
      free(result_data);
    }
  }
  else {
    // Continue reading if we need more data
    int r = uv_read_start(stream, on_alloc, on_get_read);
    if (r != 0) {
      fprintf(stderr, "Error restarting read: %s\n", uv_strerror(r));

      // Make a local copy of callback and data
      memcache_cb cb      = req_data->cb;
      void*       cb_data = req_data->data;
      // Set connection data to NULL before releasing/freeing
      connection->data = NULL;

      // Mark the request as being handled
      req_data->has_been_freed = true;

      // Release the connection
      mc_release_connection(connection);

      // Free the request data
      req_get_free(req_data);

      // Call callback with error
      if (cb && cb_data) {
        cb(cb_data, NULL, 0);
      }
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
    free(req); // Free the write request struct itself
    return;
  }

  // Nullify the pointer in req_data before freeing req locally
  req_data->write_req = NULL;

  // Store connection reference for safety
  mc_conn_t* connection = req_data->connection;
  if (!connection) {
    fprintf(stderr, "Error: NULL connection in on_get_write\n");

    // Make a local copy of callback and data
    memcache_cb cb      = req_data->cb;
    void*       cb_data = req_data->data;

    // Mark as freed
    req_data->has_been_freed = true;

    // Free resources
    req_get_free(req_data);
    free(req); // Free the write request struct itself

    // Call callback with error after cleanup
    if (cb) {
      cb(cb_data, NULL, 0);
    }
    return;
  }

  free(req); // Free the write request struct itself

  if (status != 0) {
    fprintf(stderr, "Error writing GET request to memcached: %s\n", uv_strerror(status));

    // Make a local copy of callback and data
    memcache_cb cb      = req_data->cb;
    void*       cb_data = req_data->data;

    // Mark as freed
    req_data->has_been_freed = true;

    // Set connection data to NULL before releasing/freeing
    connection->data = NULL;

    // Release connection and free resources
    mc_release_connection(connection);
    req_get_free(req_data);

    // Call callback with error after cleanup
    if (cb) {
      cb(cb_data, NULL, 0);
    }
  }

  int r = uv_read_start((uv_stream_t*) &connection->tcp, on_alloc, on_get_read);
  if (r != 0) {
    fprintf(stderr, "Error starting read after GET request: %s\n", uv_strerror(r));

    // Make a local copy of callback and data
    memcache_cb cb      = req_data->cb;
    void*       cb_data = req_data->data;

    // Mark as freed
    req_data->has_been_freed = true;

    // Set connection data to NULL before releasing/freeing
    connection->data = NULL;

    // Release connection and free resources
    mc_release_connection(connection);
    req_get_free(req_data);

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
    mc_queued_op_t* op = (mc_queued_op_t*) calloc(1, sizeof(mc_queued_op_t));
    if (!op) {
      cb(data, NULL, 0);
      return -1;
    }

    // Initialize the operation
    op->type       = QUEUE_OP_GET;
    op->op.get.key = strndup(key, keylen);
    if (!op->op.get.key) {
      free(op);
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
      free(op->op.get.key);
      free(op);
      cb(data, NULL, 0);
      return -1;
    }

    return 0;
  }

  // Rest of the function remains unchanged - existing implementation for when a connection is available
  mc_get_req_t* req_data = (mc_get_req_t*) calloc(1, sizeof(mc_get_req_t));
  if (!req_data) {
    mc_release_connection(connection);
    cb(data, NULL, 0);
    return -1;
  }

  req_data->client      = client;
  req_data->data        = data;
  req_data->cb          = cb;
  req_data->connection  = connection;
  req_data->key         = strndup(key, keylen);
  req_data->keylen      = keylen;
  req_data->type        = REQ_GET;
  req_data->msg[0].base = "get ";
  req_data->msg[0].len  = 4;
  req_data->msg[1].base = req_data->key;
  req_data->msg[1].len  = req_data->keylen;
  req_data->msg[2].base = "\r\n";
  req_data->msg[2].len  = 2;
  req_data->write_req   = NULL; // Initialize to NULL

  uv_write_t* req = (uv_write_t*) calloc(1, sizeof(uv_write_t));
  if (!req) {
    req_get_free(req_data); // Use req_get_free to clean up key
    mc_release_connection(connection);
    cb(data, NULL, 0);
    return -1;
  }
  req_data->write_req = req; // Store pointer to write request

  req->data        = req_data;
  connection->data = req_data;
  fprintf(stderr, "DEBUG: memcache_get assigned req %p to conn %p\n", req_data, connection);

  int r = uv_write(req, (uv_stream_t*) &connection->tcp, req_data->msg, 3, on_get_write);
  if (r != 0) {
    mc_release_connection(connection);
    req_get_free(req_data);
    cb(data, NULL, 0);
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
