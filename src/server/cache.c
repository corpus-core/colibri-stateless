#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

// Memcached client structure
typedef struct mc_s {
  uv_loop_t*   loop;
  uv_tcp_t**   connections;
  unsigned int size;
  unsigned int connected;
  unsigned int connecting;
  unsigned int available;
  unsigned int head;
  unsigned int tail;
} mc_t;

// Connection structure
typedef struct mc_conn_s {
  mc_t*     client;
  uv_tcp_t* tcp;
} mc_conn_t;

// Callback function type
typedef void (*memcache_cb)(void* data, char* value, size_t value_len);

// Request structure for get operations
typedef struct {
  mc_t*       client;
  void*       data;
  memcache_cb callback;
  mc_conn_t*  connection;
  uv_tcp_t*   tcp;
  char*       key;
  size_t      keylen;
  uv_buf_t*   msg;
} mc_get_req_t;

// Request structure for set operations
typedef struct {
  mc_t*       client;
  void*       data;
  memcache_cb callback;
  mc_conn_t*  connection;
  uv_tcp_t*   tcp;
  char*       key;
  size_t      keylen;
  char*       value;
  size_t      value_len;
  uint32_t    ttl;
  uv_buf_t*   msg;
} mc_set_req_t;

// Forward declarations
static void       on_connection(uv_connect_t* req, int status);
static void       on_get_write(uv_write_t* req, int status);
static void       on_get_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
static void       on_set_write(uv_write_t* req, int status);
static void       on_set_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
static void       on_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
static mc_conn_t* mc_get_connection(mc_t* client);
static void       mc_release_connection(mc_conn_t* connection);

// Create a new memcached client
mc_t* memcache_new(unsigned int pool_size) {
  mc_t* client = (mc_t*) calloc(1, sizeof(mc_t));
  if (!client) return NULL;

  client->loop       = uv_default_loop();
  client->size       = pool_size;
  client->head       = 0;
  client->tail       = 0;
  client->available  = 0;
  client->connected  = 0;
  client->connecting = 0;

  client->connections = (uv_tcp_t**) calloc(pool_size, sizeof(uv_tcp_t*));
  if (!client->connections) {
    free(client);
    return NULL;
  }

  for (unsigned int i = 0; i < pool_size; i++) {
    uv_tcp_t* tcp = (uv_tcp_t*) calloc(1, sizeof(uv_tcp_t));
    if (!tcp) continue;

    if (uv_tcp_init(client->loop, tcp) != 0) {
      free(tcp);
      continue;
    }

    mc_conn_t* connection = (mc_conn_t*) calloc(1, sizeof(mc_conn_t));
    if (!connection) {
      uv_close((uv_handle_t*) tcp, NULL);
      free(tcp);
      continue;
    }

    connection->client = client;
    connection->tcp    = tcp;
    tcp->data          = connection;

    client->connections[i] = tcp;
  }

  return client;
}

// Connect to memcached server
int memcache_connect(mc_t* client, const char* host, int port) {
  if (!client) return -1;

  struct sockaddr_in addr;
  int                r = uv_ip4_addr(host, port, &addr);
  if (r != 0) return r;

  client->connecting = client->size;

  for (unsigned int i = 0; i < client->size; i++) {
    uv_connect_t* req = (uv_connect_t*) calloc(1, sizeof(uv_connect_t));
    if (!req) continue;

    req->data = client->connections[i]->data;

    r = uv_tcp_connect(req, client->connections[i], (const struct sockaddr*) &addr, on_connection);
    if (r != 0) {
      free(req);
      continue;
    }
  }

  return 0;
}

// Free the memcached client
void memcache_free(mc_t** client_p) {
  if (!client_p || !*client_p) return;

  mc_t* client = *client_p;

  for (unsigned int i = 0; i < client->size; i++) {
    if (client->connections[i]) {
      mc_conn_t* connection = (mc_conn_t*) client->connections[i]->data;
      if (connection) free(connection);

      uv_close((uv_handle_t*) client->connections[i], NULL);
      free(client->connections[i]);
    }
  }

  free(client->connections);
  free(client);
  *client_p = NULL;
}

// Get value from memcached
int memcache_get(mc_t* client, char* key, size_t keylen, void* data, memcache_cb cb) {
  if (!client || !key || keylen == 0 || !cb) return -1;

  mc_conn_t* connection = mc_get_connection(client);
  if (!connection) return -1;

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

  req_data->client     = client;
  req_data->data       = data;
  req_data->callback   = cb;
  req_data->connection = connection;
  req_data->tcp        = connection->tcp;
  req_data->key        = strndup(key, keylen);
  req_data->keylen     = keylen;

  uv_buf_t* msg = (uv_buf_t*) calloc(3, sizeof(uv_buf_t));
  if (!msg) {
    free(req_data->key);
    free(req_data);
    free(req);
    mc_release_connection(connection);
    return -1;
  }

  msg[0].base = "get ";
  msg[0].len  = 4;
  msg[1].base = req_data->key;
  msg[1].len  = req_data->keylen;
  msg[2].base = "\r\n";
  msg[2].len  = 2;

  req_data->msg = msg;
  req->data     = req_data;

  connection->tcp->data = req_data;

  int r = uv_write(req, (uv_stream_t*) connection->tcp, msg, 3, on_get_write);
  if (r != 0) {
    free(req_data->key);
    free(req_data->msg);
    free(req_data);
    free(req);
    mc_release_connection(connection);
    return r;
  }

  return 0;
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

  req_data->client     = client;
  req_data->data       = data;
  req_data->callback   = cb;
  req_data->connection = connection;
  req_data->tcp        = connection->tcp;
  req_data->key        = strndup(key, keylen);
  req_data->keylen     = keylen;
  req_data->value      = strndup(value, value_len);
  req_data->value_len  = value_len;
  req_data->ttl        = ttl;

  char len_str[32];
  sprintf(len_str, "%zu", value_len);

  uv_buf_t* msg = (uv_buf_t*) calloc(7, sizeof(uv_buf_t));
  if (!msg) {
    free(req_data->key);
    free(req_data->value);
    free(req_data);
    free(req);
    mc_release_connection(connection);
    return -1;
  }

  char flags_ttl[64];
  sprintf(flags_ttl, " 0 %u ", ttl);

  msg[0].base = "set ";
  msg[0].len  = 4;
  msg[1].base = req_data->key;
  msg[1].len  = req_data->keylen;
  msg[2].base = flags_ttl;
  msg[2].len  = strlen(flags_ttl);
  msg[3].base = strdup(len_str);
  msg[3].len  = strlen(len_str);
  msg[4].base = "\r\n";
  msg[4].len  = 2;
  msg[5].base = req_data->value;
  msg[5].len  = req_data->value_len;
  msg[6].base = "\r\n";
  msg[6].len  = 2;

  req_data->msg = msg;
  req->data     = req_data;

  connection->tcp->data = req_data;

  int r = uv_write(req, (uv_stream_t*) connection->tcp, msg, 7, on_set_write);
  if (r != 0) {
    free(req_data->key);
    free(req_data->value);
    free(msg[3].base);
    free(req_data->msg);
    free(req_data);
    free(req);
    mc_release_connection(connection);
    return r;
  }

  return 0;
}

// Private functions

static void on_connection(uv_connect_t* req, int status) {
  mc_conn_t* connection = (mc_conn_t*) req->data;
  mc_t*      client     = connection->client;

  free(req);

  client->connecting--;

  if (status == 0) {
    client->connected++;
    client->available++;
    client->tail = (client->tail + 1) % client->size;
  }
}

static mc_conn_t* mc_get_connection(mc_t* client) {
  if (!client || client->available == 0) return NULL;

  mc_conn_t* connection = (mc_conn_t*) client->connections[client->head]->data;
  client->head          = (client->head + 1) % client->size;
  client->available--;

  return connection;
}

static void mc_release_connection(mc_conn_t* connection) {
  if (!connection) return;

  mc_t* client = connection->client;
  client->tail = (client->tail + 1) % client->size;
  client->available++;
}

static void on_get_write(uv_write_t* req, int status) {
  mc_get_req_t* req_data = (mc_get_req_t*) req->data;

  free(req);

  if (status != 0) {
    if (req_data->callback) {
      req_data->callback(req_data->data, NULL, 0);
    }

    free(req_data->key);
    free(req_data->msg);
    free(req_data);
    mc_release_connection(req_data->connection);
    return;
  }

  int r = uv_read_start((uv_stream_t*) req_data->tcp, on_alloc, on_get_read);
  if (r != 0) {
    if (req_data->callback) {
      req_data->callback(req_data->data, NULL, 0);
    }

    free(req_data->key);
    free(req_data->msg);
    free(req_data);
    mc_release_connection(req_data->connection);
  }
}

static void on_get_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  mc_get_req_t* req_data = (mc_get_req_t*) stream->data;

  uv_read_stop(stream);

  if (nread <= 0) {
    if (buf->base) free(buf->base);
    if (req_data->callback) {
      req_data->callback(req_data->data, NULL, 0);
    }

    free(req_data->key);
    free(req_data->msg);
    free(req_data);
    stream->data = req_data->connection;
    mc_release_connection(req_data->connection);
    return;
  }

  char* response  = buf->base;
  response[nread] = '\0';

  // Check if the key was found
  if (strncmp(response, "END", 3) == 0) {
    if (req_data->callback) {
      req_data->callback(req_data->data, NULL, 0);
    }
  }
  else {
    // Parse the response
    size_t value_len = 0;
    char   key[256];

    sscanf(response, "VALUE %s 0 %zu", key, &value_len);

    if (value_len > 0) {
      // Find the start of the value
      char* value_start = strstr(response, "\r\n") + 2;

      if (req_data->callback) {
        req_data->callback(req_data->data, value_start, value_len);
      }
    }
    else {
      if (req_data->callback) {
        req_data->callback(req_data->data, NULL, 0);
      }
    }
  }

  free(buf->base);
  free(req_data->key);
  free(req_data->msg);
  free(req_data);

  stream->data = req_data->connection;
  mc_release_connection(req_data->connection);
}

static void on_set_write(uv_write_t* req, int status) {
  mc_set_req_t* req_data = (mc_set_req_t*) req->data;

  free(req);

  if (status != 0) {
    if (req_data->callback) {
      req_data->callback(req_data->data, NULL, 0);
    }

    free(req_data->key);
    free(req_data->value);
    free(req_data->msg[3].base);
    free(req_data->msg);
    free(req_data);
    mc_release_connection(req_data->connection);
    return;
  }

  int r = uv_read_start((uv_stream_t*) req_data->tcp, on_alloc, on_set_read);
  if (r != 0) {
    if (req_data->callback) {
      req_data->callback(req_data->data, NULL, 0);
    }

    free(req_data->key);
    free(req_data->value);
    free(req_data->msg[3].base);
    free(req_data->msg);
    free(req_data);
    mc_release_connection(req_data->connection);
  }
}

static void on_set_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
  mc_set_req_t* req_data = (mc_set_req_t*) stream->data;

  uv_read_stop(stream);

  if (nread <= 0) {
    if (buf->base) free(buf->base);
    if (req_data->callback) {
      req_data->callback(req_data->data, NULL, 0);
    }

    free(req_data->key);
    free(req_data->value);
    free(req_data->msg[3].base);
    free(req_data->msg);
    free(req_data);
    stream->data = req_data->connection;
    mc_release_connection(req_data->connection);
    return;
  }

  char* response  = buf->base;
  response[nread] = '\0';

  // Check if the set was successful
  if (strncmp(response, "STORED", 6) == 0) {
    if (req_data->callback) {
      req_data->callback(req_data->data, req_data->value, req_data->value_len);
    }
  }
  else {
    if (req_data->callback) {
      req_data->callback(req_data->data, NULL, 0);
    }
  }

  free(buf->base);
  free(req_data->key);
  free(req_data->value);
  free(req_data->msg[3].base);
  free(req_data->msg);
  free(req_data);

  stream->data = req_data->connection;
  mc_release_connection(req_data->connection);
}

static void on_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  buf->base = (char*) malloc(suggested_size);
  buf->len  = suggested_size;
}
