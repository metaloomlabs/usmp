#include "USMPTransport.h"

#include <string.h>

#include "mbedtls/constant_time.h"
#include "mbedtls/md.h"

// S3: session-phase UTACKs (frame type >= 5) carry an 8-byte truncated HMAC-SHA256 over
// their 7-byte header so an off-path attacker cannot forge an ACK. Handshake-phase UTACKs
// (types 1-4) predate the session keys and stay unauthenticated.
#define UTACK_HEADER_LEN 7
#define UTACK_MAC_LEN 8

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

  // Step 1: read header exactly
  if (max_len < USMP_HEADER_SIZE) return -1;
  size_t received = 0;
  while (received < USMP_HEADER_SIZE) {
    if (!ctx->client.connected()) return -1;
    if (ctx->client.available()) {
      int n = ctx->client.read(buf + received, USMP_HEADER_SIZE - received);
      if (n > 0) received += n;
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
      if (n > 0) received += n;
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
  return ctx->client.connect(ctx->host, ctx->port) ? 0 : -1;
}

static int arduino_tcp_available(usmp_transport_t* t) {
  USMPArduinoTcpCtx* ctx = (USMPArduinoTcpCtx*)t->ctx;
  if (!ctx || !ctx->client.connected()) return 0;
  return ctx->client.available();
}

// USMPTCPTransport methods

bool USMPTCPTransport::connectWiFi() const {
  if (!_ssid) return true;  // WiFi managed externally — nothing to do
  WiFi.begin(_ssid, _password);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 15000) return false;
    delay(500);
  }
  return true;
}

bool USMPTCPTransport::init(usmp_transport_t* t) const {
  USMPArduinoTcpCtx* ctx = new USMPArduinoTcpCtx();
  if (!ctx) return false;

  strncpy(ctx->host, _host, sizeof(ctx->host) - 1);
  ctx->host[sizeof(ctx->host) - 1] = '\0';
  ctx->port = _port;

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
  t->set_session_keys = NULL;  // TCP needs no UTACK authentication
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

  while (1) {
    if (ctx->rx_len > 0) {
      memcpy(temp, ctx->rx_buf, ctx->rx_len);
      n = ctx->rx_len;
      ctx->rx_len = 0;
    } else {
      int packetSize = ctx->udp.parsePacket();
      if (packetSize <= 0) {
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
    if (type < 5) {
      if (ctx->last_rx_type > 0 && type <= ctx->last_rx_type) {
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
  return ctx->udp.begin(0) ? 0 : -1;
}

static int arduino_udp_available(usmp_transport_t* t) {
  USMPArduinoUdpCtx* ctx = (USMPArduinoUdpCtx*)t->ctx;
  if (!ctx) return 0;
  return (ctx->rx_len > 0 || ctx->udp.available() > 0) ? 1 : 0;
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
bool USMPUDPTransport::connectWiFi() const {
  if (!_ssid) return true;  // WiFi managed externally — nothing to do
  WiFi.begin(_ssid, _password);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 15000) return false;
    delay(500);
  }
  return true;
}

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