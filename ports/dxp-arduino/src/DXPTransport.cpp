#include "DXPTransport.h"
#include <string.h>

// ── Transport hook implementations ────────────────────────────────────────────

static int arduino_tcp_send(dxp_transport_t *t, const uint8_t *data, size_t len)
{
    DXPArduinoTcpCtx *ctx = (DXPArduinoTcpCtx *)t->ctx;
    size_t sent = 0;
    while (sent < len) {
        size_t n = ctx->client.write(data + sent, len - sent);
        if (n == 0) return -1;
        sent += n;
    }
    return 0;
}

static int arduino_tcp_recv(dxp_transport_t *t, uint8_t *buf, size_t max_len)
{
    DXPArduinoTcpCtx *ctx = (DXPArduinoTcpCtx *)t->ctx;
    const uint32_t TIMEOUT_MS = 5000;

    // Step 1: read header exactly
    if (max_len < DXP_HEADER_SIZE) return -1;
    size_t   received = 0;
    uint32_t start    = millis();
    while (received < DXP_HEADER_SIZE) {
        if (!ctx->client.connected())        return -1;
        if (millis() - start > TIMEOUT_MS)   return -1;
        if (ctx->client.available()) {
            int n = ctx->client.read(buf + received, DXP_HEADER_SIZE - received);
            if (n > 0) received += n;
        }
        delay(1);
    }

    // Step 2: parse payload length
    uint16_t payload_len = buf[8] | (buf[9] << 8);
    if (payload_len > DXP_MAX_PAYLOAD)                   return -1;
    if (DXP_HEADER_SIZE + payload_len > max_len)         return -1;

    // Step 3: read payload exactly
    start = millis();
    while (received < DXP_HEADER_SIZE + payload_len) {
        if (!ctx->client.connected())        return -1;
        if (millis() - start > TIMEOUT_MS)   return -1;
        if (ctx->client.available()) {
            int n = ctx->client.read(buf + received,
                                     DXP_HEADER_SIZE + payload_len - received);
            if (n > 0) received += n;
        }
        delay(1);
    }

    return (int)received;
}

static void arduino_tcp_close(dxp_transport_t *t)
{
    DXPArduinoTcpCtx *ctx = (DXPArduinoTcpCtx *)t->ctx;
    if (ctx) ctx->client.stop();
    // ctx intentionally NOT deleted — host/port retained for reconnect
}

static int arduino_tcp_reconnect(dxp_transport_t *t)
{
    DXPArduinoTcpCtx *ctx = (DXPArduinoTcpCtx *)t->ctx;
    if (!ctx) return -1;
    ctx->client.stop();
    return ctx->client.connect(ctx->host, ctx->port) ? 0 : -1;
}

static int arduino_tcp_available(dxp_transport_t *t)
{
    DXPArduinoTcpCtx *ctx = (DXPArduinoTcpCtx *)t->ctx;
    if (!ctx || !ctx->client.connected()) return 0;
    return ctx->client.available();
}

// ── DXPTCPTransport methods ───────────────────────────────────────────────────

bool DXPTCPTransport::connectWiFi() const
{
    if (!_ssid) return true; // WiFi managed externally — nothing to do
    WiFi.begin(_ssid, _password);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > 15000) return false;
        delay(500);
    }
    return true;
}

bool DXPTCPTransport::init(dxp_transport_t *t) const
{
    DXPArduinoTcpCtx *ctx = new DXPArduinoTcpCtx();
    if (!ctx) return false;

    strncpy(ctx->host, _host, sizeof(ctx->host) - 1);
    ctx->host[sizeof(ctx->host) - 1] = '\0';
    ctx->port = _port;

    if (!ctx->client.connect(_host, _port)) {
        delete ctx;
        return false;
    }

    t->send      = arduino_tcp_send;
    t->recv      = arduino_tcp_recv;
    t->close     = arduino_tcp_close;
    t->reconnect = arduino_tcp_reconnect;
    t->available = arduino_tcp_available;
    t->ctx       = ctx;
    return true;
}