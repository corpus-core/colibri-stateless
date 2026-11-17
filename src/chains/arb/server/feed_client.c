/*
 * Copyright 2025,2026 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "feed_client.h"
#include "arb_conf.h"
#include "bytes.h"
#include "json.h"
#include "logger.h"
#include "server.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include "../../../util/win_compat.h"
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

/*
 * Apple's libcurl 8.7 is built without the WebSocket protocol, so `wss://`
 * fails with "Protocol not supported" even though `curl_ws_recv` is declared.
 * The handshake is a normal TLS connection (`CONNECT_ONLY`) plus the HTTP
 * upgrade, and the frames are parsed here. permessage-deflate is not offered;
 * a frame with RSV1 set is rejected instead of being inflated.
 */

#define FEED_MAX_MESSAGE      (8u * 1024u * 1024u)
#define FEED_MAX_OUT          (64u * 1024u)
#define FEED_MAX_LOGGED       32u
#define FEED_READ_CHUNK       (16u * 1024u)
#define FEED_READS_PER_TICK   32
#define FEED_INACTIVITY_MS    30000u
#define FEED_RECONNECT_MIN_MS 1000u
#define FEED_RECONNECT_MAX_MS 30000u
#define FEED_MAX_HEADER       8192u

typedef enum {
  FEED_IDLE = 0,
  FEED_CONNECTING,
  FEED_OPEN_PENDING, ///< TLS is up; waiting until libcurl's poll handle has closed
  FEED_UPGRADING,
  FEED_OPEN,
  FEED_CLOSING,
  FEED_STOPPED
} feed_phase_t;

typedef struct feed_poll_ctx {
  uv_poll_t     poll;
  curl_socket_t sock;
} feed_poll_ctx_t;

static struct {
  feed_phase_t       phase;
  CURLM*             multi;
  CURL*              easy;
  bool               easy_in_multi;
  uv_timer_t         curl_timer;
  uv_timer_t         reconnect_timer;
  uv_timer_t         inactivity_timer;
  bool               timers_ready;
  bool               reconnect_armed;
  uv_poll_t          ws_poll;
  bool               ws_poll_inited;
  feed_poll_ctx_t*   handshake;
  bool               handshake_close_pending;
  buffer_t           rx;
  uint32_t           rx_off;
  buffer_t           out;
  uint32_t           out_off;
  buffer_t           message;
  uint64_t           last_seq;
  uint32_t           reconnect_ms;
  char               error_buffer[CURL_ERROR_SIZE];
  char               chain_id[32];
  char               accept_b64[32];
  char*              url;
  bool               tls;
  char               host[256];
  char               path[256];
  int                port;
} feed;

static void start_connect(void);
static void finish_close(void);
static void drop_connection(const char* reason);
static void check_multi_info(void);
static void start_ws_after_handshake(void);
static void on_reconnect_timer(uv_timer_t* timer);
static void on_curl_timeout(uv_timer_t* timer);
static void on_handshake_poll(uv_poll_t* handle, int status, int events);
static void on_ws_poll(uv_poll_t* handle, int status, int events);
static void flush_out(void);
static void read_socket(void);

static uint32_t rol32(uint32_t v, int n) {
  return (v << n) | (v >> (32 - n));
}

/** SHA-1 of a message shorter than 120 bytes. Used only for the handshake accept value. */
static void sha1_short(const uint8_t* msg, size_t len, uint8_t out[20]) {
  uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
  uint8_t  block[128];
  memset(block, 0, sizeof(block));
  if (len > 119) len = 119;
  memcpy(block, msg, len);
  block[len]          = 0x80;
  uint64_t bits       = (uint64_t) len * 8;
  size_t   nblocks    = len < 56 ? 1 : 2;
  size_t   length_at  = nblocks * 64 - 8;
  for (int i = 0; i < 8; i++) block[length_at + i] = (uint8_t) (bits >> (56 - 8 * i));

  for (size_t b = 0; b < nblocks; b++) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
      size_t o = b * 64 + (size_t) i * 4;
      w[i]     = ((uint32_t) block[o] << 24) | ((uint32_t) block[o + 1] << 16) | ((uint32_t) block[o + 2] << 8) | block[o + 3];
    }
    for (int i = 16; i < 80; i++) w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h0, breg = h1, c = h2, d = h3, e = h4;
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) {
        f = (breg & c) | ((~breg) & d);
        k = 0x5A827999;
      }
      else if (i < 40) {
        f = breg ^ c ^ d;
        k = 0x6ED9EBA1;
      }
      else if (i < 60) {
        f = (breg & c) | (breg & d) | (c & d);
        k = 0x8F1BBCDC;
      }
      else {
        f = breg ^ c ^ d;
        k = 0xCA62C1D6;
      }
      uint32_t temp = rol32(a, 5) + f + e + k + w[i];
      e             = d;
      d             = c;
      c             = rol32(breg, 30);
      breg          = a;
      a             = temp;
    }
    h0 += a;
    h1 += breg;
    h2 += c;
    h3 += d;
    h4 += e;
  }
  uint32_t hs[5] = {h0, h1, h2, h3, h4};
  for (int i = 0; i < 5; i++) {
    out[i * 4]     = (uint8_t) (hs[i] >> 24);
    out[i * 4 + 1] = (uint8_t) (hs[i] >> 16);
    out[i * 4 + 2] = (uint8_t) (hs[i] >> 8);
    out[i * 4 + 3] = (uint8_t) hs[i];
  }
}

static void b64_encode(const uint8_t* in, size_t n, char* out) {
  static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t            o     = 0;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = ((uint32_t) in[i]) << 16;
    if (i + 1 < n) v |= ((uint32_t) in[i + 1]) << 8;
    if (i + 2 < n) v |= in[i + 2];
    out[o++] = tab[(v >> 18) & 63];
    out[o++] = tab[(v >> 12) & 63];
    out[o++] = (i + 1 < n) ? tab[(v >> 6) & 63] : '=';
    out[o++] = (i + 2 < n) ? tab[v & 63] : '=';
  }
  out[o] = 0;
}

static void fill_random(uint8_t* dst, size_t n) {
  static FILE* urandom = NULL;
  if (!urandom) urandom = fopen("/dev/urandom", "rb");
  if (urandom) {
    size_t got = fread(dst, 1, n, urandom);
    if (got == n) return;
    clearerr(urandom);
  }
  uint64_t x = uv_hrtime() ^ (uint64_t) (uintptr_t) dst;
  for (size_t i = 0; i < n; i++) {
    x      = x * 6364136223846793005ULL + 1;
    dst[i] = (uint8_t) (x >> 33);
  }
}

/** RFC 6455 section 1.3 example: key `dGhlIHNhbXBsZSBub25jZQ==` accepts as `s3pPLMBiTxaQ9kYGzzhZRbK+xOo=`. */
static bool ws_accept_selfcheck(void) {
  const char*  key = "dGhlIHNhbXBsZSBub25jZQ==";
  const char*  guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  uint8_t      digest[20];
  char         got[32];
  char         material[128];
  int          n = snprintf(material, sizeof(material), "%s%s", key, guid);
  sha1_short((const uint8_t*) material, (size_t) n, digest);
  b64_encode(digest, 20, got);
  return strcmp(got, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0;
}

static int curl_timer_cb(CURLM* multi, long timeout_ms, void* userp) {
  (void) multi;
  (void) userp;
  if (feed.phase != FEED_CONNECTING) return 0;
  if (timeout_ms < 0)
    uv_timer_stop(&feed.curl_timer);
  else
    uv_timer_start(&feed.curl_timer, on_curl_timeout, timeout_ms == 0 ? 1 : (uint64_t) timeout_ms, 0);
  return 0;
}

static void on_curl_timeout(uv_timer_t* timer) {
  (void) timer;
  if (!feed.multi || feed.phase != FEED_CONNECTING) return;
  int       running = 0;
  CURLMcode mc      = curl_multi_socket_action(feed.multi, CURL_SOCKET_TIMEOUT, 0, &running);
  if (mc != CURLM_OK) {
    drop_connection(curl_multi_strerror(mc));
    return;
  }
  check_multi_info();
}

static void on_handshake_closed(uv_handle_t* handle);

static void close_handshake_poll(void) {
  if (!feed.handshake || feed.handshake_close_pending) return;
  uv_poll_stop(&feed.handshake->poll);
  if (feed.multi) curl_multi_assign(feed.multi, feed.handshake->sock, NULL);
  feed.handshake_close_pending = true;
  uv_close((uv_handle_t*) &feed.handshake->poll, on_handshake_closed);
}

static void on_handshake_closed(uv_handle_t* handle) {
  feed_poll_ctx_t* ctx         = (feed_poll_ctx_t*) handle->data;
  feed.handshake_close_pending = false;
  if (feed.handshake == ctx) feed.handshake = NULL;
  safe_free(ctx);
  if (feed.phase == FEED_OPEN_PENDING)
    start_ws_after_handshake();
  else if (feed.phase == FEED_CLOSING || feed.phase == FEED_STOPPED)
    finish_close();
}

// libuv closes a poll handle asynchronously and allows only one poller per fd.
// Curl removes and re-adds the same fd inside one socket_action, so the poll
// handle stays alive across CURL_POLL_REMOVE and is closed only when this
// client takes the socket over or tears the connection down.
static int curl_socket_cb(CURL* easy, curl_socket_t s, int action, void* userp, void* socketp) {
  (void) easy;
  (void) userp;
  feed_poll_ctx_t* ctx = (feed_poll_ctx_t*) socketp;
  if (s == CURL_SOCKET_BAD && action != CURL_POLL_REMOVE) return 0;
  if (feed.phase != FEED_CONNECTING) {
    if (action == CURL_POLL_REMOVE && ctx) {
      uv_poll_stop(&ctx->poll);
      if (feed.multi) curl_multi_assign(feed.multi, s, NULL);
    }
    return 0;
  }

  if (action == CURL_POLL_REMOVE) {
    if (ctx) {
      uv_poll_stop(&ctx->poll);
      curl_multi_assign(feed.multi, s, NULL);
    }
    return 0;
  }

  if (!ctx && feed.handshake && !feed.handshake_close_pending && feed.handshake->sock == s)
    ctx = feed.handshake;
  if (!ctx) {
    if (feed.handshake && !feed.handshake_close_pending) close_handshake_poll();
    ctx       = (feed_poll_ctx_t*) safe_calloc(1, sizeof(*ctx));
    ctx->sock = s;
    if (uv_poll_init_socket(uv_default_loop(), &ctx->poll, s) != 0) {
      log_info("Arbitrum feed poll init failed for fd %d", (int) s);
      safe_free(ctx);
      return -1;
    }
    ctx->poll.data = ctx;
    feed.handshake = ctx;
  }
  curl_multi_assign(feed.multi, s, ctx);
  int events = 0;
  if (action == CURL_POLL_IN || action == CURL_POLL_INOUT) events |= UV_READABLE;
  if (action == CURL_POLL_OUT || action == CURL_POLL_INOUT) events |= UV_WRITABLE;
  uv_poll_start(&ctx->poll, events, on_handshake_poll);
  return 0;
}

static void on_handshake_poll(uv_poll_t* handle, int status, int events) {
  if (feed.phase != FEED_CONNECTING) return;
  if (!handle || !handle->data) return;
  feed_poll_ctx_t* ctx = (feed_poll_ctx_t*) handle->data;
  if (status < 0) {
    drop_connection(uv_strerror(status));
    return;
  }
  int flags = 0;
  if (events & UV_READABLE) flags |= CURL_CSELECT_IN;
  if (events & UV_WRITABLE) flags |= CURL_CSELECT_OUT;
  int       running = 0;
  CURLMcode mc      = curl_multi_socket_action(feed.multi, ctx->sock, flags, &running);
  if (mc != CURLM_OK) {
    drop_connection(curl_multi_strerror(mc));
    return;
  }
  check_multi_info();
}

static void ensure_multi(void) {
  if (feed.multi) return;
  feed.multi = curl_multi_init();
  if (!feed.multi) {
    log_error("Arbitrum feed: curl_multi_init failed");
    return;
  }
  curl_multi_setopt(feed.multi, CURLMOPT_SOCKETFUNCTION, curl_socket_cb);
  curl_multi_setopt(feed.multi, CURLMOPT_TIMERFUNCTION, curl_timer_cb);
}

static void ensure_timers(void) {
  if (feed.timers_ready) return;
  uv_loop_t* loop = uv_default_loop();
  uv_timer_init(loop, &feed.curl_timer);
  uv_timer_init(loop, &feed.reconnect_timer);
  uv_timer_init(loop, &feed.inactivity_timer);
  feed.timers_ready = true;
  feed.reconnect_ms = FEED_RECONNECT_MIN_MS;
}

static void schedule_reconnect(void) {
  if (feed.phase == FEED_STOPPED || feed.reconnect_armed) return;
  feed.reconnect_armed = true;
  uint32_t delay       = feed.reconnect_ms ? feed.reconnect_ms : FEED_RECONNECT_MIN_MS;
  log_info("Arbitrum feed reconnect in %d ms", delay);
  uv_timer_start(&feed.reconnect_timer, on_reconnect_timer, delay, 0);
  uint32_t next = delay * 2;
  if (next > FEED_RECONNECT_MAX_MS) next = FEED_RECONNECT_MAX_MS;
  feed.reconnect_ms = next;
}

static void on_reconnect_timer(uv_timer_t* timer) {
  (void) timer;
  feed.reconnect_armed = false;
  if (feed.phase != FEED_IDLE) return;
  start_connect();
}

static void release_easy(void) {
  if (feed.easy && feed.easy_in_multi && feed.multi) {
    curl_multi_remove_handle(feed.multi, feed.easy);
    feed.easy_in_multi = false;
  }
  if (feed.easy) {
    curl_easy_cleanup(feed.easy);
    feed.easy = NULL;
  }
  feed.rx.data.len      = 0;
  feed.rx_off           = 0;
  feed.out.data.len     = 0;
  feed.out_off          = 0;
  feed.message.data.len = 0;
}

static void finish_close(void) {
  bool reconnect = feed.phase == FEED_CLOSING || feed.phase == FEED_OPEN_PENDING || feed.phase == FEED_UPGRADING;
  bool stopped   = feed.phase == FEED_STOPPED;
  uv_timer_stop(&feed.inactivity_timer);
  release_easy();
  if (stopped) {
    if (feed.multi) {
      curl_multi_cleanup(feed.multi);
      feed.multi = NULL;
    }
    safe_free(feed.url);
    feed.url = NULL;
    buffer_free(&feed.rx);
    buffer_free(&feed.out);
    buffer_free(&feed.message);
    return;
  }
  if (!reconnect) return;
  feed.phase = FEED_IDLE;
  schedule_reconnect();
}

static void on_ws_closed(uv_handle_t* handle) {
  (void) handle;
  feed.ws_poll_inited = false;
  finish_close();
}

static void close_ws_poll(void) {
  if (!feed.ws_poll_inited) return;
  feed.ws_poll_inited = false;
  uv_poll_stop(&feed.ws_poll);
  uv_close((uv_handle_t*) &feed.ws_poll, on_ws_closed);
}

static void drop_connection(const char* reason) {
  if (feed.phase == FEED_STOPPED || feed.phase == FEED_CLOSING) return;
  log_info("Arbitrum feed disconnected (%s)", reason ? reason : "unknown");
  feed.phase = FEED_CLOSING;
  uv_timer_stop(&feed.inactivity_timer);
  uv_timer_stop(&feed.curl_timer);
  if (feed.ws_poll_inited) {
    close_ws_poll();
    return;
  }
  if (feed.handshake && !feed.handshake_close_pending) {
    close_handshake_poll();
    return;
  }
  if (feed.handshake_close_pending) return;
  finish_close();
}

static void on_inactivity(uv_timer_t* timer) {
  (void) timer;
  drop_connection("no feed data for 30s");
}

static void watch_ws(int events) {
  if (!feed.ws_poll_inited) return;
  if (feed.phase != FEED_OPEN && feed.phase != FEED_UPGRADING) return;
  uv_poll_start(&feed.ws_poll, events, on_ws_poll);
}

static void flush_out(void) {
  while (feed.out_off < feed.out.data.len && feed.easy) {
    size_t   sent = 0;
    CURLcode rc   = curl_easy_send(feed.easy, feed.out.data.data + feed.out_off, feed.out.data.len - feed.out_off, &sent);
    feed.out_off += (uint32_t) sent;
    if (rc == CURLE_AGAIN) {
      watch_ws(UV_READABLE | UV_WRITABLE);
      return;
    }
    if (rc != CURLE_OK) {
      drop_connection(curl_easy_strerror(rc));
      return;
    }
    if (sent == 0) {
      drop_connection("socket send stalled");
      return;
    }
  }
  feed.out.data.len = 0;
  feed.out_off      = 0;
  watch_ws(UV_READABLE);
}

static void queue_out(const void* data, size_t n) {
  if (!n || feed.phase == FEED_CLOSING || feed.phase == FEED_STOPPED) return;
  uint32_t queued = feed.out.data.len - feed.out_off;
  if (n > FEED_MAX_OUT || queued > FEED_MAX_OUT - (uint32_t) n) {
    drop_connection("websocket send buffer exceeded");
    return;
  }
  buffer_append(&feed.out, bytes((uint8_t*) data, (uint32_t) n));
  flush_out();
}

static void send_ws_frame(uint8_t opcode, const uint8_t* data, size_t len) {
  if (len > 125) len = 125;
  uint8_t mask[4];
  uint8_t frame[2 + 4 + 125];
  fill_random(mask, 4);
  frame[0] = (uint8_t) (0x80 | opcode);
  frame[1] = (uint8_t) (0x80 | len);
  memcpy(frame + 2, mask, 4);
  for (size_t i = 0; i < len; i++) frame[6 + i] = data[i] ^ mask[i & 3];
  queue_out(frame, 6 + len);
}

static void log_feed_message(void) {
  size_t len = feed.message.data.len;
  if (buffer_grow(&feed.message, len + 1) < len + 1) {
    drop_connection("out of memory");
    return;
  }
  feed.message.data.data[len] = 0;
  json_t root                  = json_parse((char*) feed.message.data.data);
  feed.message.data.len        = 0;
  if (root.type != JSON_TYPE_OBJECT) {
    log_info("Arbitrum feed ignored non-object payload (%d bytes)", (uint32_t) len);
    return;
  }

  json_t messages = json_get(root, "messages");
  if (messages.type == JSON_TYPE_ARRAY) {
    uint32_t seen = 0;
    json_for_each_value(messages, msg) {
      if (seen >= FEED_MAX_LOGGED) {
        log_info("Arbitrum feed batch truncated after %d messages", FEED_MAX_LOGGED);
        break;
      }
      seen++;
      if (msg.type != JSON_TYPE_OBJECT) continue;
      json_t   seq_json = json_get(msg, "sequenceNumber");
      uint64_t seq      = json_as_uint64(seq_json);
      if (seq_json.type == JSON_TYPE_NUMBER) feed.last_seq = seq;
      log_info("Arbitrum feed seq=%l blockHash=%j", seq, json_get(msg, "blockHash"));
    }
  }

  json_t confirmed = json_get(root, "confirmedSequenceNumberMessage");
  if (confirmed.type == JSON_TYPE_OBJECT)
    log_info("Arbitrum feed confirmed up to seq %l", json_get_uint64(confirmed, "sequenceNumber"));
}

static bool header_has(const char* header, size_t header_len, const char* key, char* value, size_t value_cap) {
  size_t key_len = strlen(key);
  for (size_t i = 0; i + key_len < header_len; i++) {
    if ((i == 0 || header[i - 1] == '\n') && strncasecmp(header + i, key, key_len) == 0) {
      const char* v = header + i + key_len;
      size_t      vn = header_len - (size_t) (v - header);
      while (vn && (*v == ' ' || *v == '\t')) {
        v++;
        vn--;
      }
      size_t take = 0;
      while (take < vn && v[take] != '\r' && v[take] != '\n') take++;
      while (take && v[take - 1] == ' ') take--;
      if (take >= value_cap) take = value_cap - 1;
      memcpy(value, v, take);
      value[take] = 0;
      return true;
    }
  }
  return false;
}

static void take_payload(const uint8_t* data, size_t n, bool fin) {
  if (feed.message.data.len > FEED_MAX_MESSAGE || n > FEED_MAX_MESSAGE - feed.message.data.len) {
    drop_connection("feed message exceeds 8MiB");
    return;
  }
  if (n) buffer_append(&feed.message, bytes((uint8_t*) data, (uint32_t) n));
  if (fin) log_feed_message();
}

static void compact_rx(void) {
  if (!feed.rx_off) return;
  if (feed.rx_off > feed.rx.data.len) feed.rx_off = feed.rx.data.len;
  buffer_splice(&feed.rx, 0, feed.rx_off, NULL_BYTES);
  feed.rx_off = 0;
}

static void parse_frames(void) {
  while (feed.phase == FEED_OPEN && feed.rx.data.len - feed.rx_off >= 2) {
    uint8_t* p = feed.rx.data.data + feed.rx_off;
    uint32_t n = feed.rx.data.len - feed.rx_off;
    uint8_t  b0 = p[0];
    uint8_t  b1 = p[1];
    if (b0 & 0x70) {
      drop_connection("compressed websocket frame; permessage-deflate is not enabled");
      return;
    }
    if (b1 & 0x80) {
      drop_connection("server sent a masked frame");
      return;
    }
    uint64_t plen = b1 & 0x7f;
    uint32_t hdr  = 2;
    if (plen == 126) {
      if (n < 4) break;
      plen = ((uint64_t) p[2] << 8) | p[3];
      hdr  = 4;
    }
    else if (plen == 127) {
      if (n < 10) break;
      plen = 0;
      for (int i = 0; i < 8; i++) plen = (plen << 8) | p[2 + i];
      hdr = 10;
      if (p[2] & 0x80) {
        drop_connection("websocket frame length has the high bit set");
        return;
      }
    }
    if (plen > FEED_MAX_MESSAGE || hdr + plen < hdr) {
      drop_connection("websocket frame is too large");
      return;
    }
    if ((uint64_t) n < (uint64_t) hdr + plen) break;
    const uint8_t* payload = p + hdr;
    uint8_t        opcode  = b0 & 0x0f;
    bool           fin     = (b0 & 0x80) != 0;
    if ((opcode & 0x8) && (!fin || plen > 125)) {
      drop_connection("illegal websocket control frame");
      return;
    }
    if (opcode == 0x8) {
      drop_connection("server closed the websocket");
      return;
    }
    if (opcode == 0x9) send_ws_frame(0xA, payload, (size_t) plen);
    else if (opcode == 0xA) {
    }
    else if (opcode == 0x1 || opcode == 0x2 || opcode == 0x0)
      take_payload(payload, (size_t) plen, fin);
    else {
      drop_connection("unknown websocket opcode");
      return;
    }
    if (feed.phase != FEED_OPEN) break;
    feed.rx_off += hdr + (uint32_t) plen;
  }
  compact_rx();
}

static void finish_upgrade(void) {
  uint8_t* p     = feed.rx.data.data;
  uint32_t n     = feed.rx.data.len;
  uint32_t end   = 0;
  bool     found = false;
  for (; end + 3 < n; end++) {
    if (p[end] == '\r' && p[end + 1] == '\n' && p[end + 2] == '\r' && p[end + 3] == '\n') {
      end += 4;
      found = true;
      break;
    }
  }
  if (!found) {
    if (n > FEED_MAX_HEADER) drop_connection("websocket upgrade header is too large");
    return;
  }

  char chain[32] = {0};
  char accept[64] = {0};
  if (header_has((char*) p, end, "arbitrum-chain-id:", chain, sizeof(chain)))
    snprintf(feed.chain_id, sizeof(feed.chain_id), "%s", chain);
  bool status_101 = n >= 12 && memcmp(p, "HTTP/1.", 7) == 0 && memcmp(p + 8, " 101", 4) == 0;
  bool accept_ok  = header_has((char*) p, end, "sec-websocket-accept:", accept, sizeof(accept)) &&
                   strcmp(accept, feed.accept_b64) == 0;
  if (!status_101 || !accept_ok) {
    char line[80];
    size_t line_len = 0;
    while (line_len < sizeof(line) - 1 && line_len < end && p[line_len] != '\r') line_len++;
    memcpy(line, p, line_len);
    line[line_len] = 0;
    for (size_t i = 0; i < line_len; i++) {
      if (line[i] == '\n' || line[i] == '\r') line[i] = ' ';
    }
    log_info("Arbitrum feed upgrade rejected: %s", line[0] ? line : "empty response");
    drop_connection("websocket upgrade was not accepted");
    return;
  }

  buffer_splice(&feed.rx, 0, end, NULL_BYTES);
  feed.phase        = FEED_OPEN;
  feed.reconnect_ms = FEED_RECONNECT_MIN_MS;
  uv_timer_start(&feed.inactivity_timer, on_inactivity, FEED_INACTIVITY_MS, 0);
  log_info("Arbitrum feed connected to %s (chain %s)", feed.url, feed.chain_id[0] ? feed.chain_id : "?");
  parse_frames();
}

static void read_socket(void) {
  uint8_t chunk[FEED_READ_CHUNK];
  for (int i = 0; i < FEED_READS_PER_TICK && (feed.phase == FEED_OPEN || feed.phase == FEED_UPGRADING); i++) {
    size_t   n  = 0;
    CURLcode rc = curl_easy_recv(feed.easy, chunk, sizeof(chunk), &n);
    if (rc == CURLE_AGAIN) break;
    if (rc != CURLE_OK || n == 0) {
      drop_connection(rc == CURLE_OK ? "connection closed" : curl_easy_strerror(rc));
      return;
    }
    if (feed.rx.data.len > FEED_MAX_MESSAGE || n > FEED_MAX_MESSAGE - feed.rx.data.len) {
      drop_connection("feed socket buffer exceeds 8MiB");
      return;
    }
    buffer_append(&feed.rx, bytes(chunk, (uint32_t) n));
    uv_timer_start(&feed.inactivity_timer, on_inactivity, FEED_INACTIVITY_MS, 0);
  }
  if (feed.phase == FEED_UPGRADING) finish_upgrade();
  else if (feed.phase == FEED_OPEN) parse_frames();
}

static void on_ws_poll(uv_poll_t* handle, int status, int events) {
  (void) handle;
  if (feed.phase != FEED_OPEN && feed.phase != FEED_UPGRADING) return;
  if (status < 0) {
    drop_connection(uv_strerror(status));
    return;
  }
  if (events & UV_WRITABLE) flush_out();
  if ((feed.phase == FEED_OPEN || feed.phase == FEED_UPGRADING) && (events & UV_READABLE)) read_socket();
}

static void queue_upgrade(void) {
  uint8_t key_raw[16];
  char    key_b64[32];
  fill_random(key_raw, sizeof(key_raw));
  b64_encode(key_raw, sizeof(key_raw), key_b64);

  char     material[96];
  uint8_t  digest[20];
  int      material_len = snprintf(material, sizeof(material), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key_b64);
  sha1_short((const uint8_t*) material, (size_t) material_len, digest);
  b64_encode(digest, 20, feed.accept_b64);

  char req[1024];
  char host_port[32] = "";
  if ((feed.tls && feed.port != 443) || (!feed.tls && feed.port != 80))
    snprintf(host_port, sizeof(host_port), ":%d", feed.port);
  char seq_line[80] = "";
  if (feed.last_seq)
    snprintf(seq_line, sizeof(seq_line), "Arbitrum-Requested-Sequence-Number: %llu\r\n",
             (unsigned long long) feed.last_seq);
  int req_len = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s%s\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Key: %s\r\n"
                     "Sec-WebSocket-Version: 13\r\n"
                     "Arbitrum-Feed-Client-Version: 2\r\n"
                     "%s"
                     "\r\n",
                     feed.path, feed.host, host_port, key_b64, seq_line);
  if (req_len < 0 || (size_t) req_len >= sizeof(req)) {
    drop_connection("upgrade request does not fit");
    return;
  }
  queue_out(req, (size_t) req_len);
}

static void start_ws_after_handshake(void) {
  curl_socket_t sock = CURL_SOCKET_BAD;
  CURLcode      info = curl_easy_getinfo(feed.easy, CURLINFO_ACTIVESOCKET, &sock);
  if (info != CURLE_OK || sock == CURL_SOCKET_BAD) {
    feed.phase = FEED_CLOSING;
    finish_close();
    return;
  }
  int rc = uv_poll_init_socket(uv_default_loop(), &feed.ws_poll, sock);
  if (rc != 0) {
    log_info("Arbitrum feed poll init failed: %s", uv_strerror(rc));
    feed.phase = FEED_CLOSING;
    finish_close();
    return;
  }
  feed.ws_poll_inited = true;
  feed.ws_poll.data   = &feed;
  feed.phase          = FEED_UPGRADING;
  uv_timer_start(&feed.inactivity_timer, on_inactivity, FEED_INACTIVITY_MS, 0);
  uv_poll_start(&feed.ws_poll, UV_READABLE | UV_WRITABLE, on_ws_poll);
  queue_upgrade();
}

static void check_multi_info(void) {
  CURLMsg* msg;
  int      left = 0;
  while (feed.multi && (msg = curl_multi_info_read(feed.multi, &left))) {
    if (msg->msg != CURLMSG_DONE || msg->easy_handle != feed.easy) continue;
    uv_timer_stop(&feed.curl_timer);
    if (msg->data.result != CURLE_OK || feed.phase == FEED_STOPPED) {
      const char* err = feed.error_buffer[0] ? feed.error_buffer : curl_easy_strerror(msg->data.result);
      if (feed.phase != FEED_STOPPED)
        log_info("Arbitrum feed handshake failed: %s", err);
      feed.phase = feed.phase == FEED_STOPPED ? FEED_STOPPED : FEED_CLOSING;
      if (feed.handshake && !feed.handshake_close_pending) close_handshake_poll();
      else if (!feed.handshake_close_pending) finish_close();
      return;
    }
    // curl_easy_send/recv require the easy handle to stay inside the multi handle.
    feed.phase = FEED_OPEN_PENDING;
    if (feed.handshake_close_pending) return;
    if (feed.handshake) {
      close_handshake_poll();
      return;
    }
    start_ws_after_handshake();
  }
}

static bool parse_feed_url(const char* url) {
  const char* p = NULL;
  if (strncmp(url, "wss://", 6) == 0) {
    feed.tls  = true;
    feed.port = 443;
    p         = url + 6;
  }
  else if (strncmp(url, "ws://", 5) == 0) {
    feed.tls  = false;
    feed.port = 80;
    p         = url + 5;
  }
  else
    return false;

  const char* slash = strchr(p, '/');
  const char* end   = slash ? slash : p + strlen(p);
  const char* colon = NULL;
  for (const char* c = p; c < end; c++) {
    if (*c == ':') colon = c;
  }
  size_t host_len = (size_t) ((colon ? colon : end) - p);
  if (host_len == 0 || host_len >= sizeof(feed.host)) return false;
  memcpy(feed.host, p, host_len);
  feed.host[host_len] = 0;
  if (colon) {
    int port = atoi(colon + 1);
    if (port <= 0 || port > 65535) return false;
    feed.port = port;
  }
  if (!slash)
    snprintf(feed.path, sizeof(feed.path), "/");
  else if (strlen(slash) >= sizeof(feed.path))
    return false;
  else
    snprintf(feed.path, sizeof(feed.path), "%s", slash);
  if (strchr(feed.host, '\r') || strchr(feed.host, '\n') || strchr(feed.path, '\r') || strchr(feed.path, '\n'))
    return false;
  return feed.path[0] == '/';
}

static void start_connect(void) {
  if (feed.phase != FEED_IDLE || !feed.url || !feed.multi) return;
  feed.phase           = FEED_CONNECTING;
  feed.chain_id[0]     = 0;
  feed.error_buffer[0] = 0;
  feed.easy            = curl_easy_init();
  if (!feed.easy) {
    feed.phase = FEED_CLOSING;
    finish_close();
    return;
  }

  char curl_url[512];
  snprintf(curl_url, sizeof(curl_url), "%s://%s:%d/", feed.tls ? "https" : "http", feed.host, feed.port);
  curl_easy_setopt(feed.easy, CURLOPT_URL, curl_url);
  curl_easy_setopt(feed.easy, CURLOPT_CONNECT_ONLY, 1L);
  curl_easy_setopt(feed.easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
  curl_easy_setopt(feed.easy, CURLOPT_ERRORBUFFER, feed.error_buffer);
  curl_easy_setopt(feed.easy, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(feed.easy, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(feed.easy, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
#if LIBCURL_VERSION_NUM >= 0x075500
  curl_easy_setopt(feed.easy, CURLOPT_PROTOCOLS_STR, feed.tls ? "https" : "http");
#endif
  c4_curl_configure_ssl(feed.easy);

  CURLMcode mc = curl_multi_add_handle(feed.multi, feed.easy);
  if (mc != CURLM_OK) {
    log_info("Arbitrum feed: curl_multi_add_handle failed: %s", curl_multi_strerror(mc));
    feed.phase = FEED_CLOSING;
    finish_close();
    return;
  }
  feed.easy_in_multi = true;
  log_info("Arbitrum feed connecting to %s", feed.url);
  // curl only invokes the timer callback when the timeout changes. The first
  // socket action has to be kicked explicitly or the handshake never starts.
  int       running = 0;
  CURLMcode kick    = curl_multi_socket_action(feed.multi, CURL_SOCKET_TIMEOUT, 0, &running);
  if (kick != CURLM_OK) {
    drop_connection(curl_multi_strerror(kick));
    return;
  }
  check_multi_info();
}

void arb_feed_start(void) {
  if (feed.phase != FEED_IDLE && feed.phase != FEED_STOPPED) {
    log_info("Arbitrum feed already running");
    return;
  }
  if (!ws_accept_selfcheck()) {
    log_error("Arbitrum feed websocket accept self-check failed");
    return;
  }
  if (!arb_config.feed_url || arb_config.feed_url[0] == 0) {
    log_error("Arbitrum feed URL is empty");
    return;
  }
  if (!parse_feed_url(arb_config.feed_url)) {
    log_error("Arbitrum feed URL must be ws:// or wss:// with a host");
    return;
  }
  ensure_timers();
  ensure_multi();
  if (!feed.multi) return;
  safe_free(feed.url);
  feed.url   = bprintf(NULL, "%s", arb_config.feed_url);
  feed.phase = FEED_IDLE;
  log_info("Arbitrum feed client starting");
  start_connect();
}

void arb_feed_stop(void) {
  if (feed.phase == FEED_STOPPED) return;
  // Reconnect backoff leaves the phase idle while the timer is still active.
  // Stopping that timer is what lets the server process exit.
  if (!feed.timers_ready && !feed.easy && !feed.ws_poll_inited && !feed.handshake) return;
  feed.phase           = FEED_STOPPED;
  feed.reconnect_armed = false;
  if (feed.timers_ready) {
    uv_timer_stop(&feed.reconnect_timer);
    uv_timer_stop(&feed.inactivity_timer);
    uv_timer_stop(&feed.curl_timer);
  }
  if (feed.ws_poll_inited) {
    close_ws_poll();
    return;
  }
  if (feed.handshake && !feed.handshake_close_pending) {
    close_handshake_poll();
    return;
  }
  if (feed.handshake_close_pending) return;
  finish_close();
}
