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

// Memcached client structure
typedef struct mc_s {
  uv_loop_t*         loop;
  mc_conn_t*         connections;
  unsigned int       size;
  unsigned int       connected;
  unsigned int       connecting;
  unsigned int       available;
  struct sockaddr_in server_addr;
} mc_t;

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

  client->loop        = uv_default_loop();
  client->size        = pool_size;
  client->server_addr = addr;
  client->connecting  = pool_size;
  client->connections = (mc_conn_t*) calloc(pool_size, sizeof(mc_conn_t));
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

// Free the memcached client
void memcache_free(mc_t** client_p) {
  if (!client_p || !*client_p) return;

  mc_t* client = *client_p;

  for (unsigned int i = 0; i < client->size; i++)
    uv_close((uv_handle_t*) &client->connections[i].tcp, NULL);

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

static void mc_release_connection(mc_conn_t* connection) {
  if (!connection) return;
  connection->in_use = false;
  mc_t* client       = connection->client;
  client->available++;
  //  printf(":: mc_release_connection available: %d\n", client->available);
}

/// -------- SET ---------

typedef struct {
  mc_t*       client;
  void*       data;
  memcache_cb callback;
  mc_conn_t*  connection;
  uv_buf_t    msg[3];
} mc_set_req_t;

static void req_set_free(mc_set_req_t* req) {
  free(req->msg[0].base);
  free(req->msg[1].base);
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
  if (!req_data) {
    fprintf(stderr, "Error: Invalid request data in on_set_read\n");
    if (buf && buf->base) free(buf->base);
    mc_release_connection(connection);
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

  // Call callback with appropriate result
  if (req_data->callback) {
    if (success) {
      req_data->callback(req_data->data, req_data->msg[1].base, req_data->msg[1].len);
    }
    else {
      req_data->callback(req_data->data, NULL, 0);
    }
  }

  // Release connection before freeing request to avoid race conditions
  mc_release_connection(req_data->connection);
  req_set_free(req_data);

  if (buf && buf->base) {
    free(buf->base);
  }
}

static void on_set_write(uv_write_t* req, int status) {
  // Check if request is valid
  if (!req) {
    fprintf(stderr, "Error: on_set_write called with NULL request\n");
    return;
  }

  mc_set_req_t* req_data = (mc_set_req_t*) req->data;
  if (!req_data) {
    fprintf(stderr, "Error: Invalid request data in on_set_write\n");
    free(req);
    return;
  }

  // Store connection reference for safety
  mc_conn_t* connection = req_data->connection;
  if (!connection) {
    fprintf(stderr, "Error: NULL connection in on_set_write\n");
    if (req_data->callback) {
      req_data->callback(req_data->data, NULL, 0);
    }
    req_set_free(req_data);
    free(req);
    return;
  }

  free(req);

  if (status != 0) {
    fprintf(stderr, "Error writing to memcached: %s\n", uv_strerror(status));
    // error, just call the callback with NULL
    if (req_data->callback) {
      req_data->callback(req_data->data, NULL, 0);
    }
    mc_release_connection(connection);
    req_set_free(req_data);
    return;
  }

  int r = uv_read_start((uv_stream_t*) &connection->tcp, on_alloc, on_set_read);
  if (r != 0) {
    fprintf(stderr, "Error starting read: %s\n", uv_strerror(r));
    if (req_data->callback) {
      req_data->callback(req_data->data, NULL, 0);
    }
    mc_release_connection(connection);
    req_set_free(req_data);
  }
}

// Set value in memcached
int memcache_set(mc_t* client, char* key, size_t keylen, char* value, size_t value_len,
                 uint32_t ttl, void* data, memcache_cb cb) {
  if (!client || !key || keylen == 0 || !value) return -1;

  mc_conn_t* connection = mc_get_connection(client);
  if (!connection) return -1;

  mc_set_req_t* req_data = (mc_set_req_t*) calloc(1, sizeof(mc_set_req_t));
  if (!req_data) {
    mc_release_connection(connection);
    return -1;
  }

  uv_write_t* req = (uv_write_t*) calloc(1, sizeof(uv_write_t));
  if (!req) {
    free(req_data);
    mc_release_connection(connection);
    return -1;
  }

  req_data->client      = client;
  req_data->data        = data;
  req_data->callback    = cb;
  req_data->connection  = connection;
  buffer_t command_buf  = {0};
  req_data->msg[0].base = bprintf(&command_buf, "set %s 0 %d %d\r\n", key, (uint32_t) ttl, (uint32_t) value_len);
  req_data->msg[0].len  = command_buf.data.len;
  req_data->msg[1].base = (char*) bytes_dup(bytes(value, value_len)).data;
  req_data->msg[1].len  = value_len;
  req_data->msg[2].base = "\r\n";
  req_data->msg[2].len  = 2;
  req->data             = req_data;
  connection->data      = req_data;

  int r = uv_write(req, (uv_stream_t*) &connection->tcp, req_data->msg, 3, on_set_write);
  if (r != 0) {
    mc_release_connection(connection);
    req_set_free(req_data);
    return r;
  }

  return 0;
}

/// -------- GET ---------

// Request structure for get operations
typedef struct {
  mc_t*       client;
  void*       data;
  memcache_cb cb;
  mc_conn_t*  connection;
  char*       key; // key to get
  size_t      keylen;
  uv_buf_t    msg[3];
  buffer_t    buffer; // data received
} mc_get_req_t;

static void req_get_free(mc_get_req_t* req) {
  free(req->key);
  buffer_free(&req->buffer);
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
  if (!req_data) {
    fprintf(stderr, "Error: Invalid request data in on_get_read\n");
    if (buf && buf->base) free(buf->base);
    mc_release_connection(connection);
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

    // Release connection first to make it available for reuse
    mc_release_connection(connection);

    // Call callback with result
    if (cb) {
      cb(cb_data, (char*) result.data, result.len);
    }

    // Clean up request data
    req_get_free(req_data);
  }
  else {
    // Continue reading if we need more data
    int r = uv_read_start(stream, on_alloc, on_get_read);
    if (r != 0) {
      fprintf(stderr, "Error restarting read: %s\n", uv_strerror(r));

      // Call callback with error
      if (req_data->cb) {
        req_data->cb(req_data->data, NULL, 0);
      }

      // Cleanup resources
      mc_release_connection(connection);
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
  if (!req_data) {
    fprintf(stderr, "Error: Invalid request data in on_get_write\n");
    free(req);
    return;
  }

  // Store connection reference for safety
  mc_conn_t* connection = req_data->connection;
  if (!connection) {
    fprintf(stderr, "Error: NULL connection in on_get_write\n");
    if (req_data->cb) {
      req_data->cb(req_data->data, NULL, 0);
    }
    req_get_free(req_data);
    free(req);
    return;
  }

  free(req);

  if (status != 0) {
    fprintf(stderr, "Error writing GET request to memcached: %s\n", uv_strerror(status));
    if (req_data->cb) {
      req_data->cb(req_data->data, NULL, 0);
    }
    mc_release_connection(connection);
    req_get_free(req_data);
    return;
  }

  int r = uv_read_start((uv_stream_t*) &connection->tcp, on_alloc, on_get_read);
  if (r != 0) {
    fprintf(stderr, "Error starting read after GET request: %s\n", uv_strerror(r));
    if (req_data->cb) {
      req_data->cb(req_data->data, NULL, 0);
    }
    mc_release_connection(connection);
    req_get_free(req_data);
  }
}

// Get value from memcached
int memcache_get(mc_t* client, char* key, size_t keylen, void* data, memcache_cb cb) {
  if (!client) {
    cb(data, NULL, 0);
    return 0;
  }
  if (!key || keylen == 0 || !cb) return -1;

  mc_conn_t* connection = mc_get_connection(client);
  if (!connection) {
    cb(data, NULL, 0);
    return 0;
  }

  mc_get_req_t* req_data = (mc_get_req_t*) calloc(1, sizeof(mc_get_req_t));
  if (!req_data) {
    mc_release_connection(connection);
    return -1;
  }

  uv_write_t* req = (uv_write_t*) calloc(1, sizeof(uv_write_t));
  if (!req) {
    free(req_data);
    mc_release_connection(connection);
    return -1;
  }

  req_data->client      = client;
  req_data->data        = data;
  req_data->cb          = cb;
  req_data->connection  = connection;
  req_data->key         = strndup(key, keylen);
  req_data->keylen      = keylen;
  req_data->msg[0].base = "get ";
  req_data->msg[0].len  = 4;
  req_data->msg[1].base = req_data->key;
  req_data->msg[1].len  = req_data->keylen;
  req_data->msg[2].base = "\r\n";
  req_data->msg[2].len  = 2;
  req->data             = req_data;
  connection->data      = req_data;

  int r = uv_write(req, (uv_stream_t*) &connection->tcp, req_data->msg, 3, on_get_write);
  if (r != 0) {
    mc_release_connection(connection);
    req_get_free(req_data);
    return r;
  }

  return 0;
}
