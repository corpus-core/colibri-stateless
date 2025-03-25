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
  buf->base = (char*) malloc(suggested_size);
  buf->len  = suggested_size;
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
  if (!client || client->available == 0) return NULL;
  printf(":: mc_get_connection available: %d\n", client->available);
  for (int i = 0; i < client->size; i++) {
    mc_conn_t* connection = client->connections + i;
    if (!connection->in_use) {
      client->available--;
      connection->in_use = true;
      return connection;
    }
  }

  return NULL;
}

static void mc_release_connection(mc_conn_t* connection) {
  if (!connection) return;
  connection->in_use = false;
  mc_t* client       = connection->client;
  client->available++;
  printf(":: mc_release_connection available: %d\n", client->available);
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
  mc_conn_t*    connection = (mc_conn_t*) stream->data;
  mc_set_req_t* req_data   = (mc_set_req_t*) connection->data;

  uv_read_stop(stream);

  // Check for connection errors
  if (nread < 0 && nread != UV_EOF) {
    printf(":: connection error in set operation: %s\n", uv_strerror(nread));
  }

  if (nread <= 0) {
    if (buf->base) free(buf->base);
    if (req_data->callback)
      req_data->callback(req_data->data, NULL, 0);
    mc_release_connection(req_data->connection);
    req_set_free(req_data);
    return;
  }

  char* response  = buf->base;
  response[nread] = '\0';

  // Check if the set was successful
  if (strncmp(response, "STORED", 6) == 0) {
    if (req_data->callback)
      req_data->callback(req_data->data, req_data->msg[1].base, req_data->msg[1].len);
  }
  else {
    if (req_data->callback)
      req_data->callback(req_data->data, NULL, 0);
  }
  mc_release_connection(req_data->connection);
  req_set_free(req_data);
  free(buf->base);
}

static void on_set_write(uv_write_t* req, int status) {
  mc_set_req_t* req_data = (mc_set_req_t*) req->data;
  free(req);

  if (status != 0) {
    // error, just call the callback with NULL
    req_data->callback(req_data->data, NULL, 0);
    mc_release_connection(req_data->connection);
    req_set_free(req_data);
    return;
  }

  int r = uv_read_start((uv_stream_t*) &req_data->connection->tcp, on_alloc, on_set_read);
  if (r != 0) {
    req_data->callback(req_data->data, NULL, 0);
    mc_release_connection(req_data->connection);
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
  mc_conn_t*    connection = (mc_conn_t*) stream->data;
  mc_get_req_t* req_data   = (mc_get_req_t*) connection->data;
  if (!req_data) return;

  bool    done   = false;
  bytes_t result = NULL_BYTES;

  // Check for connection errors
  if (nread < 0 && nread != UV_EOF)
    printf(":: connection error in get operation: %s\n", uv_strerror(nread));

  // fetch data
  if (nread == UV_EOF)
    done = true;
  else if (nread < 0)
    done = true;
  else
    buffer_append(&req_data->buffer, bytes(buf->base, nread));

  free(buf->base);

  // check if we have the complete response
  char* response   = (char*) req_data->buffer.data.data;
  char* end_header = NULL;
  for (int i = 0; i + 1 < req_data->buffer.data.len; i++) {
    if (response[i] == '\r' && response[i + 1] == '\n') {
      end_header = &response[i];
      break;
    }
  }
  if (req_data->buffer.data.len >= 3 && strncmp(response, "END", 3) == 0)
    done = true;
  else if (end_header) {
    size_t value_len = 0;
    char   key[256];
    char   flags[32];
    char   cas[32];

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

  if (done) {
    // Stop reading before calling callback to prevent potential re-entrancy issues
    uv_read_stop(stream);

    // Call callback with result
    req_data->cb(req_data->data, (char*) result.data, result.len);

    // Clean up resources in correct order
    mc_release_connection(connection);
    req_get_free(req_data);
  }
}

static void on_get_write(uv_write_t* req, int status) {
  mc_get_req_t* req_data = (mc_get_req_t*) req->data;
  if (!req_data) return;
  free(req);

  if (status != 0) {
    printf(":: error writing get request: %s\n", uv_strerror(status));
    req_data->cb(req_data->data, NULL, 0);
    mc_release_connection(req_data->connection);
    req_get_free(req_data);
    return;
  }

  int r = uv_read_start((uv_stream_t*) &req_data->connection->tcp, on_alloc, on_get_read);
  if (r != 0) {
    req_data->cb(req_data->data, NULL, 0);
    mc_release_connection(req_data->connection);
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
