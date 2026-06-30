#include "USMPTransport.h"
#include <string.h>

// Transport hook implementations ────────────────────────────────────────────

static int arduino_tcp_send(usmp_transport_t *t, const uint8_t *data,
                            size_t len) {
  USMPArduinoTcpCtx *ctx = (USMPArduinoTcpCtx *)t->ctx;
  size_t sent = 0;
  while (sent < len) {
    size_t n = ctx->client.write(data + sent, len - sent);
    if (n == 0)
      return -1;
    sent += n;
  }
  return 0;
}

static int arduino_tcp_recv(usmp_transport_t *t, uint8_t *buf, size_t max_len) {
  USMPArduinoTcpCtx *ctx = (USMPArduinoTcpCtx *)t->ctx;
  const uint32_t TIMEOUT_MS = 500;

  // Step 1: read header exactly
  if (max_len < USMP_HEADER_SIZE)
    return -1;
  size_t received = 0;
  uint32_t start = millis();
  while (received < USMP_HEADER_SIZE) {
    if (!ctx->client.connected())
      return -1;
    if (millis() - start > TIMEOUT_MS)
      return -1;
    if (ctx->client.available()) {
      int n = ctx->client.read(buf + received, USMP_HEADER_SIZE - received);
      if (n > 0)
        received += n;
    }
    delay(1);
  }

  // Step 2: parse payload length
  uint16_t payload_len = buf[8] | (buf[9] << 8);
  if (payload_len > USMP_MAX_PAYLOAD)
    return -1;
  if (USMP_HEADER_SIZE + payload_len > max_len)
    return -1;

  // Step 3: read payload exactly
  start = millis();
  while (received < USMP_HEADER_SIZE + payload_len) {
    if (!ctx->client.connected())
      return -1;
    if (millis() - start > TIMEOUT_MS)
      return -1;
    if (ctx->client.available()) {
      int n = ctx->client.read(buf + received,
                               USMP_HEADER_SIZE + payload_len - received);
      if (n > 0)
        received += n;
    }
    delay(1);
  }

  return (int)received;
}

static void arduino_tcp_close(usmp_transport_t *t) {
  USMPArduinoTcpCtx *ctx = (USMPArduinoTcpCtx *)t->ctx;
  if (ctx)
    ctx->client.stop();
  // ctx intentionally NOT deleted — host/port retained for reconnect
}

static void arduino_tcp_destroy(usmp_transport_t *t) {
  USMPArduinoTcpCtx *ctx = (USMPArduinoTcpCtx *)t->ctx;
  if (ctx) {
    ctx->client.stop();
    delete ctx;
    t->ctx = NULL;
  }
}

static int arduino_tcp_reconnect(usmp_transport_t *t) {
  USMPArduinoTcpCtx *ctx = (USMPArduinoTcpCtx *)t->ctx;
  if (!ctx)
    return -1;
  ctx->client.stop();
  return ctx->client.connect(ctx->host, ctx->port) ? 0 : -1;
}

static int arduino_tcp_available(usmp_transport_t *t) {
  USMPArduinoTcpCtx *ctx = (USMPArduinoTcpCtx *)t->ctx;
  if (!ctx || !ctx->client.connected())
    return 0;
  return ctx->client.available();
}

// USMPTCPTransport methods

bool USMPTCPTransport::connectWiFi() const {
  if (!_ssid)
    return true; // WiFi managed externally — nothing to do
  WiFi.begin(_ssid, _password);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 15000)
      return false;
    delay(500);
  }
  return true;
}

bool USMPTCPTransport::init(usmp_transport_t *t) const {
  USMPArduinoTcpCtx *ctx = new USMPArduinoTcpCtx();
  if (!ctx)
    return false;

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
  t->ctx = ctx;
  return true;
}

// ── UDP transport hooks

static int arduino_udp_send(usmp_transport_t *t, const uint8_t *data, size_t len) {
  USMPArduinoUdpCtx *ctx = (USMPArduinoUdpCtx *)t->ctx;
  if (!ctx) return -1;

  uint8_t type = 0;
  uint32_t seq = 0;
  bool expect_ack = false;
  if (len >= 8) {
    type = data[3];
    seq = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
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
    while (millis() - start_ms < 100) {
      int packetSize = ctx->udp.parsePacket();
      if (packetSize <= 0) {
        delay(1);
        continue;
      }

      int n = ctx->udp.read(temp, sizeof(temp));
      if (n <= 0) continue;

      // Check if it is a transport UTACK
      if (n >= 7 && temp[0] == 0xAC && temp[1] == 0xAC) {
        uint8_t ack_type = temp[2];
        uint32_t ack_seq = temp[3] | (temp[4] << 8) | (temp[5] << 16) | (temp[6] << 24);
        if (ack_type == type && ack_seq == seq) {
          return 0; // Success! ACK received
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

  return -1; // Retries exhausted
}

static int arduino_udp_recv(usmp_transport_t *t, uint8_t *buf, size_t max_len) {
  USMPArduinoUdpCtx *ctx = (USMPArduinoUdpCtx *)t->ctx;
  if (!ctx) return -1;

  uint8_t temp[USMP_HEADER_SIZE + USMP_MAX_PAYLOAD];
  int n = 0;

  const uint32_t TIMEOUT_MS = 5000;
  uint32_t start = millis();

  while (1) {
    if (ctx->rx_len > 0) {
      memcpy(temp, ctx->rx_buf, ctx->rx_len);
      n = ctx->rx_len;
      ctx->rx_len = 0;
    } else {
      if (millis() - start > TIMEOUT_MS) {
        return -1;
      }
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
    uint32_t seq = temp[4] | (temp[5] << 8) | (temp[6] << 16) | (temp[7] << 24);

    // Send UTACK back immediately
    uint8_t utack[7] = {0xAC, 0xAC, type, (uint8_t)(seq & 0xFF), (uint8_t)((seq >> 8) & 0xFF), (uint8_t)((seq >> 16) & 0xFF), (uint8_t)((seq >> 24) & 0xFF)};
    ctx->udp.beginPacket(ctx->host, ctx->port);
    ctx->udp.write(utack, sizeof(utack));
    ctx->udp.endPacket();

    // Duplicate detection (only for active sessions, type >= 5)
    if (type >= 5) {
      if (ctx->last_rx_seq_set && seq <= ctx->last_rx_seq) {
        continue; // Discard duplicate
      }
      ctx->last_rx_seq = seq;
      ctx->last_rx_seq_set = true;
    }

    if ((size_t)n > max_len) return -1;
    memcpy(buf, temp, n);
    return n;
  }
}

static void arduino_udp_close(usmp_transport_t *t) {
  USMPArduinoUdpCtx *ctx = (USMPArduinoUdpCtx *)t->ctx;
  if (ctx) {
    ctx->udp.stop();
  }
}

static void arduino_udp_destroy(usmp_transport_t *t) {
  USMPArduinoUdpCtx *ctx = (USMPArduinoUdpCtx *)t->ctx;
  if (ctx) {
    ctx->udp.stop();
    delete ctx;
    t->ctx = NULL;
  }
}

static int arduino_udp_reconnect(usmp_transport_t *t) {
  USMPArduinoUdpCtx *ctx = (USMPArduinoUdpCtx *)t->ctx;
  if (!ctx) return -1;
  ctx->udp.stop();
  ctx->rx_len = 0;
  ctx->last_rx_seq_set = false;
  return ctx->udp.begin(0) ? 0 : -1;
}

static int arduino_udp_available(usmp_transport_t *t) {
  USMPArduinoUdpCtx *ctx = (USMPArduinoUdpCtx *)t->ctx;
  if (!ctx) return 0;
  return (ctx->rx_len > 0 || ctx->udp.available() > 0) ? 1 : 0;
}

// USMPUDPTransport methods

bool USMPUDPTransport::connectWiFi() const {
  if (!_ssid)
    return true; // WiFi managed externally — nothing to do
  WiFi.begin(_ssid, _password);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 15000)
      return false;
    delay(500);
  }
  return true;
}

bool USMPUDPTransport::init(usmp_transport_t *t) const {
  USMPArduinoUdpCtx *ctx = new USMPArduinoUdpCtx();
  if (!ctx)
    return false;

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
  t->ctx = ctx;
  return true;
}