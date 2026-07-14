#include "USMPTransport.h"

#include <string.h>

extern "C" {
#include "mbedtls/constant_time.h"
#include "mbedtls/md.h"
}

// Shared WiFi bring-up (identical for TCP and UDP) ───────────────────────────
bool USMPTransportBase::connectWiFi() const {
  if (!_ssid) return true;  // WiFi managed externally — nothing to do
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.begin(_ssid, _password);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 30000) return false;
    delay(500);
  }
  return true;
}

// S3: session-phase UTACKs (frame type >= 5) carry an 8-byte truncated HMAC-SHA256 over
// their 7-byte header so an off-path attacker cannot forge an ACK. Handshake-phase UTACKs
// (types 1-4) predate the session keys and stay unauthenticated.
#define UTACK_HEADER_LEN 7
#define UTACK_MAC_LEN 8

/*
 * Session-phase UDP recv timeout (ms).
 *
 * Handshake reads are unbounded — they MUST block for the server's CHALLENGE /
 * SESSION_OK reply. Session reads (after keys are installed) are bounded so a
 * stray/duplicate UTACK or idle socket can't wedge maintain() in an infinite
 * parsePacket() spin. On timeout the recv returns 0 ("no data"), which the core
 * treats as a non-fatal empty read.
 *
 * The core's usmp_recv() retries the transport up to ~10x per call, so the
 * effective budget for an in-flight fragment to arrive is ~10x this value
 * (≈500 ms at the default). That comfortably covers WiFi round-trips while
 * keeping the worst-case maintain() stall bounded.
 */
#ifndef USMP_UDP_RECV_TIMEOUT_MS
#define USMP_UDP_RECV_TIMEOUT_MS 50
#endif

/*
 * Session-phase TCP no-progress stall timeout (ms).
 *
 * Like the UDP timeout, handshake reads are unbounded (they must wait for the
 * server reply). Once the session is established, a peer that sends a partial
 * frame and then stalls must not wedge maintain() forever. This is a *no-
 * progress* timeout: it only fires when zero bytes arrive for this long, so a
 * large-but-progressing frame is never cut off. A stall before any byte is a
 * non-fatal empty read (0); a stall mid-frame tears down (partial bytes are
 * already consumed from the stream and cannot be un-read, so we must resync).
 */
#ifndef USMP_TCP_RECV_TIMEOUT_MS
#define USMP_TCP_RECV_TIMEOUT_MS 2000
#endif

// S3: 8-byte truncated HMAC-SHA256 over the 7-byte UTACK header.
static void utack_mac(const uint8_t* key, const uint8_t* header, uint8_t out[UTACK_MAC_LEN]) {
  uint8_t full[32];
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(info, key, 32, header, UTACK_HEADER_LEN, full);
  memcpy(out, full, UTACK_MAC_LEN);
}

// Transport hook implementations ────────────────────────────────────────────

static int arduino_tcp_send(usmp_transport_t* t, const uint8_t* data, size_t len) {
  USMPArduinoTcpCtx* ctx = (USMPArduinoTcpCtx*)t->ctx;
  size_t sent = 0;
  while (sent < len) {
    size_t n = ctx->client.write(data + sent, len - sent);
    if (n == 0) return -1;
    sent += n;
  }
  return 0;
}

static int arduino_tcp_recv(usmp_transport_t* t, uint8_t* buf, size_t max_len) {
  USMPArduinoTcpCtx* ctx = (USMPArduinoTcpCtx*)t->ctx;

  // Session reads use a no-progress stall timeout (see USMP_TCP_RECV_TIMEOUT_MS);
  // handshake reads stay unbounded. last_progress advances on every byte read, so
  // a large-but-flowing frame never times out. millis() subtraction is wrap-safe.
  const bool bounded = ctx->session_active;
  uint32_t last_progress = millis();

  // Step 1: read header exactly
  if (max_len < USMP_HEADER_SIZE) return -1;
  size_t received = 0;
  while (received < USMP_HEADER_SIZE) {
    if (!ctx->client.connected()) return -1;
    if (ctx->client.available()) {
      int n = ctx->client.read(buf + received, USMP_HEADER_SIZE - received);
      if (n > 0) {
        received += n;
        last_progress = millis();
      }
    }
    if (bounded && (millis() - last_progress) >= USMP_TCP_RECV_TIMEOUT_MS) {
      // Stall before any byte is a non-fatal empty read; stall after partial
      // bytes were consumed forces a resync (we can't un-read a TCP stream).
      return received == 0 ? 0 : -1;
    }
    delay(1);
  }

  // Step 2: parse payload length
  uint16_t payload_len = buf[8] | (buf[9] << 8);
  if (payload_len > USMP_MAX_PAYLOAD) return -1;
  if (USMP_HEADER_SIZE + payload_len > max_len) return -1;

  // Step 3: read payload exactly
  while (received < USMP_HEADER_SIZE + payload_len) {
    if (!ctx->client.connected()) return -1;
    if (ctx->client.available()) {
      int n = ctx->client.read(buf + received, USMP_HEADER_SIZE + payload_len - received);
      if (n > 0) {
        received += n;
        last_progress = millis();
      }
    }
    if (bounded && (millis() - last_progress) >= USMP_TCP_RECV_TIMEOUT_MS) {
      return -1;  // stalled mid-frame — header already consumed, must resync
    }
    delay(1);
  }

  return (int)received;
}

static void arduino_tcp_close(usmp_transport_t* t) {
  USMPArduinoTcpCtx* ctx = (USMPArduinoTcpCtx*)t->ctx;
  if (ctx) ctx->client.stop();
  // ctx intentionally NOT deleted — host/port retained for reconnect
}

static void arduino_tcp_destroy(usmp_transport_t* t) {
  USMPArduinoTcpCtx* ctx = (USMPArduinoTcpCtx*)t->ctx;
  if (ctx) {
    ctx->client.stop();
    delete ctx;
    t->ctx = NULL;
  }
}

static int arduino_tcp_reconnect(usmp_transport_t* t) {
  USMPArduinoTcpCtx* ctx = (USMPArduinoTcpCtx*)t->ctx;
  if (!ctx) return -1;
  ctx->client.stop();
  // Back to handshake phase: recv must block unbounded for the server reply
  // until set_session_keys() re-marks the session active on success.
  ctx->session_active = false;
  return ctx->client.connect(ctx->host, ctx->port) ? 0 : -1;
}

static int arduino_tcp_available(usmp_transport_t* t) {
  USMPArduinoTcpCtx* ctx = (USMPArduinoTcpCtx*)t->ctx;
  if (!ctx || !ctx->client.connected()) return 0;
  return ctx->client.available();
}

// TCP does not authenticate UTACKs, so the derived keys are unused here. We wire
// this core callback (fired once the handshake completes) only to mark the
// session active, which switches recv() from the unbounded handshake path to the
// bounded stall-timeout path.
static void arduino_tcp_set_session_keys(usmp_transport_t* t, const uint8_t* tx_key,
                                         const uint8_t* rx_key) {
  (void)tx_key;
  (void)rx_key;
  USMPArduinoTcpCtx* ctx = (USMPArduinoTcpCtx*)t->ctx;
  if (ctx) ctx->session_active = true;
}

// USMPTCPTransport methods

bool USMPTCPTransport::init(usmp_transport_t* t) const {
  USMPArduinoTcpCtx* ctx = new USMPArduinoTcpCtx();
  if (!ctx) return false;

  strncpy(ctx->host, _host, sizeof(ctx->host) - 1);
  ctx->host[sizeof(ctx->host) - 1] = '\0';
  ctx->port = _port;
  ctx->session_active = false;  // handshake runs first with unbounded recv

  if (!ctx->client.connect(_host, _port)) {
    delete ctx;
    return false;
  }

  t->send = arduino_tcp_send;
  t->recv = arduino_tcp_recv;
  t->close = arduino_tcp_close;
  t->reconnect = arduino_tcp_reconnect;
  t->available = arduino_tcp_available;
  t->destroy = arduino_tcp_destroy;
  t->confirm_authenticated = NULL;
  // Not for UTACK auth (TCP has none) — only to flip recv() to its bounded
  // stall-timeout path once the handshake completes. See arduino_tcp_recv.
  t->set_session_keys = arduino_tcp_set_session_keys;
  t->ctx = ctx;
  return true;
}

// UDP transport hooks

static int arduino_udp_send(usmp_transport_t* t, const uint8_t* data, size_t len) {
  USMPArduinoUdpCtx* ctx = (USMPArduinoUdpCtx*)t->ctx;
  if (!ctx) return -1;

  uint8_t type = 0;
  uint32_t seq = 0;
  bool expect_ack = false;
  if (len >= 8) {
    type = data[3];
    seq =
        data[4] | ((uint32_t)data[5] << 8) | ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
    expect_ack = true;
  }

  if (!expect_ack) {
    ctx->udp.beginPacket(ctx->host, ctx->port);
    ctx->udp.write(data, len);
    return ctx->udp.endPacket() ? 0 : -1;
  }

  // Stop-and-wait ARQ
  uint8_t temp[USMP_HEADER_SIZE + USMP_MAX_PAYLOAD];
  for (int attempt = 0; attempt < 5; attempt++) {
    ctx->udp.beginPacket(ctx->host, ctx->port);
    ctx->udp.write(data, len);
    if (!ctx->udp.endPacket()) return -1;

    uint32_t start_ms = millis();
    while (millis() - start_ms < 500) {
      int packetSize = ctx->udp.parsePacket();
      if (packetSize <= 0) {
        delay(1);
        continue;
      }

      int n = ctx->udp.read(temp, sizeof(temp));
      if (n <= 0) continue;

      // Check if it is a transport UTACK
      if (n >= UTACK_HEADER_LEN && temp[0] == 0xAC && temp[1] == 0xAC) {
        uint8_t ack_type = temp[2];
        uint32_t ack_seq = temp[3] | ((uint32_t)temp[4] << 8) | ((uint32_t)temp[5] << 16) |
                           ((uint32_t)temp[6] << 24);
        if (ack_type == type && ack_seq == seq) {
          // S3: a session-phase ACK (type >= 5) must carry a valid MAC keyed by tx_key;
          // drop forged or unauthenticated ACKs so an off-path attacker can't spoof one.
          if (type >= 5) {
            if (!ctx->keys_set || n < UTACK_HEADER_LEN + UTACK_MAC_LEN) continue;
            uint8_t expected[UTACK_MAC_LEN];
            utack_mac(ctx->tx_key, temp, expected);
            if (mbedtls_ct_memcmp(expected, temp + UTACK_HEADER_LEN, UTACK_MAC_LEN) != 0) continue;
          }
          return 0;  // Success! ACK received (and authenticated for session frames)
        }
        continue;
      }

      // Buffer data packet received while waiting for UTACK
      if (n >= USMP_HEADER_SIZE && ctx->rx_len == 0) {
        memcpy(ctx->rx_buf, temp, n);
        ctx->rx_len = n;
      }
    }
  }

  return -1;  // Retries exhausted
}

static int arduino_udp_recv(usmp_transport_t* t, uint8_t* buf, size_t max_len) {
  USMPArduinoUdpCtx* ctx = (USMPArduinoUdpCtx*)t->ctx;
  if (!ctx) return -1;

  uint8_t temp[USMP_HEADER_SIZE + USMP_MAX_PAYLOAD];
  int n = 0;

  // Bound the wait once session keys are installed so maintain() can't spin
  // forever on a stray UTACK / idle socket; the handshake keeps the original
  // unbounded wait for the server reply. millis() subtraction is wrap-safe.
  const bool bounded = ctx->keys_set;
  const uint32_t start = millis();

  while (1) {
    if (ctx->rx_len > 0) {
      memcpy(temp, ctx->rx_buf, ctx->rx_len);
      n = ctx->rx_len;
      ctx->rx_len = 0;
    } else {
      int packetSize = ctx->udp.parsePacket();
      if (packetSize <= 0) {
        if (bounded && (millis() - start) >= USMP_UDP_RECV_TIMEOUT_MS) {
          return 0;  // no data within budget — non-fatal empty read
        }
        delay(1);
        continue;
      }
      n = ctx->udp.read(temp, sizeof(temp));
      if (n <= 0) continue;
    }

    if (n < USMP_HEADER_SIZE) continue;

    // Discard unexpected transport UTACKs
    if (temp[0] == 0xAC && temp[1] == 0xAC) {
      continue;
    }

    uint16_t magic = temp[0] | (temp[1] << 8);
    if (magic != 0xABCD) continue;

    uint8_t type = temp[3];
    uint32_t seq =
        temp[4] | ((uint32_t)temp[5] << 8) | ((uint32_t)temp[6] << 16) | ((uint32_t)temp[7] << 24);

    // Send UTACK back immediately. S3: authenticate session-phase UTACKs (type >= 5)
    // with rx_key once keys are established; handshake UTACKs stay plaintext.
    uint8_t utack[UTACK_HEADER_LEN + UTACK_MAC_LEN] = {0xAC,
                                                       0xAC,
                                                       type,
                                                       (uint8_t)(seq & 0xFF),
                                                       (uint8_t)((seq >> 8) & 0xFF),
                                                       (uint8_t)((seq >> 16) & 0xFF),
                                                       (uint8_t)((seq >> 24) & 0xFF)};
    size_t utack_len = UTACK_HEADER_LEN;
    if (type >= 5 && ctx->keys_set) {
      utack_mac(ctx->rx_key, utack, utack + UTACK_HEADER_LEN);
      utack_len = UTACK_HEADER_LEN + UTACK_MAC_LEN;
    }
    ctx->udp.beginPacket(ctx->host, ctx->port);
    ctx->udp.write(utack, utack_len);
    ctx->udp.endPacket();

    // Duplicate detection
    if (type < 5 || type == 0x0A) {
      bool is_duplicate = (type == ctx->last_rx_type) || (type == 0x0A && ctx->last_rx_type > 0) ||
                          (type == 2 && ctx->last_rx_type == 4);
      if (is_duplicate) {
        continue;  // Discard duplicate/old handshake packet
      }
      ctx->last_rx_type = type;
    }

    if ((size_t)n > max_len) return -1;
    memcpy(buf, temp, n);
    return n;
  }
}

static void arduino_udp_close(usmp_transport_t* t) {
  USMPArduinoUdpCtx* ctx = (USMPArduinoUdpCtx*)t->ctx;
  if (ctx) {
    ctx->udp.stop();
  }
}

static void arduino_udp_destroy(usmp_transport_t* t) {
  USMPArduinoUdpCtx* ctx = (USMPArduinoUdpCtx*)t->ctx;
  if (ctx) {
    ctx->udp.stop();
    delete ctx;
    t->ctx = NULL;
  }
}

static int arduino_udp_reconnect(usmp_transport_t* t) {
  USMPArduinoUdpCtx* ctx = (USMPArduinoUdpCtx*)t->ctx;
  if (!ctx) return -1;
  ctx->udp.stop();
  ctx->rx_len = 0;
  ctx->last_rx_seq_set = false;
  ctx->last_rx_type = 0;
  // Drop stale session keys: the reconnect handshake runs unauthenticated (types
  // 1-4) and must use the unbounded recv path, exactly like the first connect.
  // usmp_connect() reinstalls fresh keys via set_session_keys() on success.
  ctx->keys_set = false;
  return ctx->udp.begin(0) ? 0 : -1;
}

static int arduino_udp_available(usmp_transport_t* t) {
  USMPArduinoUdpCtx* ctx = (USMPArduinoUdpCtx*)t->ctx;
  if (!ctx) return 0;

  // Already have a datagram staged for recv() to consume.
  if (ctx->rx_len > 0) return 1;

  /*
   * WiFiUDP::available() only reports bytes left in the packet a prior
   * parsePacket() already pulled in — it does NOT peek the socket for queued
   * datagrams. So checking it here never sees an unsolicited server->device
   * message; the datagram would sit unread until the next send() happened to
   * pull it in. Instead, actively poll: parsePacket() the next datagram and
   * stage it into rx_buf so recv() (which reads rx_len first) can deliver it.
   *
   * This has the side effect of consuming one datagram from the socket, but
   * that datagram is preserved in rx_buf — nothing is lost. Stray transport
   * UTACKs and non-USMP junk are dropped here rather than staged, so
   * available() only reports genuinely deliverable frames. (Arduino is
   * single-threaded, so this never races arduino_udp_send()'s ARQ loop.)
   */
  int ps = ctx->udp.parsePacket();
  if (ps <= 0) return 0;
  int n = ctx->udp.read(ctx->rx_buf, sizeof(ctx->rx_buf));
  if (n < USMP_HEADER_SIZE) return 0;                      // too short (incl. UTACKs) — drop
  if (ctx->rx_buf[0] == 0xAC && ctx->rx_buf[1] == 0xAC) return 0;  // stray UTACK — drop
  uint16_t magic = ctx->rx_buf[0] | (ctx->rx_buf[1] << 8);
  if (magic != 0xABCD) return 0;                           // not a USMP frame — drop
  ctx->rx_len = n;
  return 1;
}

static void arduino_udp_confirm_authenticated(usmp_transport_t* t, uint32_t seq) {
  USMPArduinoUdpCtx* ctx = (USMPArduinoUdpCtx*)t->ctx;
  if (ctx) {
    ctx->last_rx_seq = seq;
    ctx->last_rx_seq_set = true;
  }
}

static void arduino_udp_set_session_keys(usmp_transport_t* t, const uint8_t* tx_key,
                                         const uint8_t* rx_key) {
  USMPArduinoUdpCtx* ctx = (USMPArduinoUdpCtx*)t->ctx;
  if (!ctx) return;
  memcpy(ctx->tx_key, tx_key, 32);
  memcpy(ctx->rx_key, rx_key, 32);
  ctx->keys_set = true;
}

// USMPUDPTransport methods
bool USMPUDPTransport::init(usmp_transport_t* t) const {
  USMPArduinoUdpCtx* ctx = new USMPArduinoUdpCtx();
  if (!ctx) return false;

  strncpy(ctx->host, _host, sizeof(ctx->host) - 1);
  ctx->host[sizeof(ctx->host) - 1] = '\0';
  ctx->port = _port;
  ctx->rx_len = 0;
  ctx->last_rx_seq_set = false;

  if (!ctx->udp.begin(0)) {
    delete ctx;
    return false;
  }

  t->send = arduino_udp_send;
  t->recv = arduino_udp_recv;
  t->close = arduino_udp_close;
  t->reconnect = arduino_udp_reconnect;
  t->available = arduino_udp_available;
  t->destroy = arduino_udp_destroy;
  t->confirm_authenticated = arduino_udp_confirm_authenticated;
  t->set_session_keys = arduino_udp_set_session_keys;
  t->ctx = ctx;
  return true;
}