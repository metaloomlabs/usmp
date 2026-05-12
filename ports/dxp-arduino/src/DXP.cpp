#include "DXP.h"
#include <string.h>
#include <stdio.h>

DXPClient::DXPClient(const char *psk)
    : _psk(psk),
      _initialized(false),
      _backoff_ms(2000),
      _last_attempt_ms(0),
      _on_connect(nullptr),
      _on_disconnect(nullptr),
      _on_reconnect(nullptr),
      _on_message(nullptr)
{
    memset(&_ctx, 0, sizeof(_ctx));
    memset(&_transport, 0, sizeof(_transport));
    memset(_rx_buf, 0, sizeof(_rx_buf));
}

// ── Internal helpers ──────────────────────────────────────────────────────────

void DXPClient::_apply_psk()
{
    _ctx.psk     = (const uint8_t *)_psk;
    _ctx.psk_len = strlen(_psk);
}

bool DXPClient::_do_reconnect()
{
    _apply_psk();
    return dxp_reconnect(&_ctx) == 0;
}

// ── begin ─────────────────────────────────────────────────────────────────────

bool DXPClient::begin(DXPTCPTransport transport)
{
    // ── WiFi ──────────────────────────────────────────────────────────────────
    if (transport._ssid) {
        Serial.printf("[DXP] Connecting to WiFi: %s\n", transport._ssid);
        if (!transport.connectWiFi()) {
            Serial.println("[DXP] WiFi connect failed");
            return false;
        }
        Serial.printf("[DXP] WiFi connected — IP: %s\n",
                      WiFi.localIP().toString().c_str());
    }

    // ── TCP transport init ────────────────────────────────────────────────────
    memset(&_transport, 0, sizeof(_transport));
    if (!transport.init(&_transport)) {
        Serial.println("[DXP] TCP connect failed");
        return false;
    }

    // ── DXP handshake ─────────────────────────────────────────────────────────
    memset(&_ctx, 0, sizeof(_ctx));
    _apply_psk();
    _ctx.keepalive_ms = 30000; // 30s default

    if (dxp_connect(&_ctx, &_transport) != 0) {
        Serial.println("[DXP] Handshake failed");
        return false;
    }

    _initialized      = true;
    _backoff_ms       = 2000;
    _last_attempt_ms  = 0;

    if (_on_connect) _on_connect();
    return true;
}

// ── send ──────────────────────────────────────────────────────────────────────

bool DXPClient::send(const char *str)
{
    return send((const uint8_t *)str, strlen(str));
}

bool DXPClient::send(const String &str)
{
    return send((const uint8_t *)str.c_str(), str.length());
}

bool DXPClient::send(const uint8_t *data, size_t len)
{
    if (!_ctx.established) return false;
    return dxp_send(&_ctx, data, (uint16_t)len) == 0;
}

// ── receive ───────────────────────────────────────────────────────────────────

bool DXPClient::available()
{
    if (!_ctx.established)       return false;
    if (!_transport.available)   return false;
    return _transport.available(&_transport) > 0;
}

String DXPClient::read()
{
    int n = dxp_recv(&_ctx, _rx_buf, sizeof(_rx_buf));
    if (n <= 0) {
        _ctx.established = false;
        if (_on_disconnect) _on_disconnect();
        return String();
    }
    return String((char *)_rx_buf, n);
}

int DXPClient::read(uint8_t *buf, size_t max_len)
{
    int n = dxp_recv(&_ctx, buf, max_len);
    if (n < 0) {
        _ctx.established = false;
        if (_on_disconnect) _on_disconnect();
    }
    return n;
}

// ── state ─────────────────────────────────────────────────────────────────────

bool DXPClient::alive()
{
    return dxp_is_connected(&_ctx);
}

String DXPClient::deviceId()
{
    char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             _ctx.device_id[0], _ctx.device_id[1], _ctx.device_id[2],
             _ctx.device_id[3], _ctx.device_id[4], _ctx.device_id[5]);
    return String(buf);
}

String DXPClient::sessionId()
{
    char buf[9];
    snprintf(buf, sizeof(buf), "%02x%02x%02x%02x",
             _ctx.session_id[0], _ctx.session_id[1],
             _ctx.session_id[2], _ctx.session_id[3]);
    return String(buf);
}

// ── keepalive ─────────────────────────────────────────────────────────────────

void DXPClient::keepalive(uint32_t ms)
{
    _ctx.keepalive_ms = ms;
}

// ── maintain ──────────────────────────────────────────────────────────────────

void DXPClient::maintain()
{
    if (!_initialized) return;

    // ── Dead — attempt reconnect with exponential backoff ─────────────────────
    if (!_ctx.established) {
        uint32_t now = millis();
        if (now - _last_attempt_ms < _backoff_ms) return;
        _last_attempt_ms = now;

        if (_do_reconnect()) {
            _backoff_ms = 2000;
            if (_on_reconnect)  _on_reconnect();
            if (_on_connect)    _on_connect();
        } else {
            if (_backoff_ms < 30000) _backoff_ms *= 2;
        }
        return;
    }

    // ── Alive — send keepalive PING if idle ───────────────────────────────────
    if (dxp_keepalive_tick(&_ctx) < 0) {
        _ctx.established = false;
        if (_on_disconnect) _on_disconnect();
        return;
    }

    // ── Non-blocking receive — fire onMessage if data waiting ─────────────────
    if (_on_message && available()) {
        int n = dxp_recv(&_ctx, _rx_buf, sizeof(_rx_buf));
        if (n > 0) {
            _on_message(_rx_buf, (size_t)n);
        } else if (n < 0) {
            _ctx.established = false;
            if (_on_disconnect) _on_disconnect();
        }
    }
}

// ── callbacks ─────────────────────────────────────────────────────────────────

void DXPClient::onConnect   (void (*cb)())                            { _on_connect    = cb; }
void DXPClient::onDisconnect(void (*cb)())                            { _on_disconnect = cb; }
void DXPClient::onReconnect (void (*cb)())                            { _on_reconnect  = cb; }
void DXPClient::onMessage   (void (*cb)(const uint8_t*, size_t len))  { _on_message    = cb; }

// ── manual control ────────────────────────────────────────────────────────────

bool DXPClient::reconnect()
{
    bool ok = _do_reconnect();
    if (ok) {
        if (_on_reconnect) _on_reconnect();
        if (_on_connect)   _on_connect();
    }
    return ok;
}

void DXPClient::close()
{
    dxp_close(&_ctx);
    _initialized = false;
}