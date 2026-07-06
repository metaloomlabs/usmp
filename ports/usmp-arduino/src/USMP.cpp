#include "USMP.h"

#include <stdio.h>
#include <string.h>

USMPClient::USMPClient(const char* psk)
    : _psk(psk),
      _initialized(false),
      _backoff_ms(2000),
      _last_attempt_ms(0),
      _on_connect(nullptr),
      _on_disconnect(nullptr),
      _on_reconnect(nullptr),
      _on_message(nullptr) {
  memset(&_ctx, 0, sizeof(_ctx));
  memset(&_transport, 0, sizeof(_transport));
  memset(_rx_buf, 0, sizeof(_rx_buf));
}

USMPClient::~USMPClient() { close(); }

// Internal helpers ──────────────────────────────────────────────────────────

void USMPClient::_apply_psk() {
  _ctx.psk = (const uint8_t*)_psk;
  _ctx.psk_len = strlen(_psk);
}

bool USMPClient::_do_reconnect() {
  _apply_psk();
  return usmp_reconnect(&_ctx) == 0;
}

// begin ─────────────────────────────────────────────────────────────────────

bool USMPClient::begin(USMPTCPTransport transport) {
  // WiFi
  if (transport._ssid) {
    if (usmp_get_log_level() >= USMP_LOG_LEVEL_INFO) {
      Serial.printf("[USMP] Connecting to WiFi: %s\n", transport._ssid);
    }
    if (!transport.connectWiFi()) {
      if (usmp_get_log_level() >= USMP_LOG_LEVEL_ERROR) {
        Serial.println("[usmp] [usmp]: WiFi connect failed");
      }
      return false;
    }
    if (usmp_get_log_level() >= USMP_LOG_LEVEL_INFO) {
      Serial.printf("[USMP] WiFi connected — IP: %s\n", WiFi.localIP().toString().c_str());
    }
  }

  // TCP transport init ────────────────────────────────────────────────────
  memset(&_transport, 0, sizeof(_transport));
  if (!transport.init(&_transport)) {
    if (usmp_get_log_level() >= USMP_LOG_LEVEL_ERROR) {
      Serial.println("[usmp] [usmp]: TCP connect failed");
    }
    return false;
  }

  // USMP handshake ─────────────────────────────────────────────────────────
  memset(&_ctx, 0, sizeof(_ctx));
  _apply_psk();
  _ctx.keepalive_ms = 30000;  // 30s default

  if (usmp_connect(&_ctx, &_transport) != 0) {
    if (usmp_get_log_level() >= USMP_LOG_LEVEL_ERROR) {
      Serial.println("[usmp] [usmp]: Handshake failed");
    }
    return false;
  }

  _initialized = true;
  _backoff_ms = 2000;
  _last_attempt_ms = 0;

  if (_on_connect) _on_connect();
  return true;
}

bool USMPClient::begin(USMPUDPTransport transport) {
  // WiFi
  if (transport._ssid) {
    if (usmp_get_log_level() >= USMP_LOG_LEVEL_INFO) {
      Serial.printf("[USMP] Connecting to WiFi: %s\n", transport._ssid);
    }
    if (!transport.connectWiFi()) {
      if (usmp_get_log_level() >= USMP_LOG_LEVEL_ERROR) {
        Serial.println("[usmp] [usmp]: WiFi connect failed");
      }
      return false;
    }
    if (usmp_get_log_level() >= USMP_LOG_LEVEL_INFO) {
      Serial.printf("[USMP] WiFi connected — IP: %s\n", WiFi.localIP().toString().c_str());
    }
  }

  // UDP transport init ────────────────────────────────────────────────────
  memset(&_transport, 0, sizeof(_transport));
  if (!transport.init(&_transport)) {
    if (usmp_get_log_level() >= USMP_LOG_LEVEL_ERROR) {
      Serial.println("[usmp] [usmp]: UDP connect failed");
    }
    return false;
  }

  // USMP handshake ─────────────────────────────────────────────────────────
  memset(&_ctx, 0, sizeof(_ctx));
  _apply_psk();
  _ctx.keepalive_ms = 30000;  // 30s default

  if (usmp_connect(&_ctx, &_transport) != 0) {
    if (usmp_get_log_level() >= USMP_LOG_LEVEL_ERROR) {
      Serial.println("[usmp] [usmp]: Handshake failed");
    }
    return false;
  }

  _initialized = true;
  _backoff_ms = 2000;
  _last_attempt_ms = 0;

  if (_on_connect) _on_connect();
  return true;
}

// send ──────────────────────────────────────────────────────────────────────

bool USMPClient::send(const char* str) { return send((const uint8_t*)str, strlen(str)); }

bool USMPClient::send(const String& str) { return send((const uint8_t*)str.c_str(), str.length()); }

bool USMPClient::send(const uint8_t* data, size_t len) {
  if (!_ctx.established) return false;
  if (usmp_send(&_ctx, data, (uint16_t)len) != 0) {
    _ctx.established = false;
    if (_on_disconnect) _on_disconnect();
    return false;
  }
  return true;
}

// receive ───────────────────────────────────────────────────────────────────

bool USMPClient::available() {
  if (!_ctx.established) return false;
  if (!_transport.available) return false;
  return _transport.available(&_transport) > 0;
}

String USMPClient::read() {
  int n = usmp_recv(&_ctx, _rx_buf, sizeof(_rx_buf));
  if (n < 0) {
    _ctx.established = false;
    if (_on_disconnect) _on_disconnect();
    return String();
  }
  if (n == 0) {
    return String();
  }
  return String((char*)_rx_buf, n);
}

int USMPClient::read(uint8_t* buf, size_t max_len) {
  int n = usmp_recv(&_ctx, buf, max_len);
  if (n < 0) {
    _ctx.established = false;
    if (_on_disconnect) _on_disconnect();
  }
  return n;
}

// state ─────────────────────────────────────────────────────────────────────

bool USMPClient::alive() { return usmp_is_connected(&_ctx); }

String USMPClient::deviceId() {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", _ctx.device_id[0], _ctx.device_id[1],
           _ctx.device_id[2], _ctx.device_id[3], _ctx.device_id[4], _ctx.device_id[5]);
  return String(buf);
}

String USMPClient::sessionId() {
  char buf[33];
  for (int i = 0; i < 16; i++) {
    snprintf(buf + (i * 2), 3, "%02x", _ctx.session_id[i]);
  }
  return String(buf);
}

// keepalive ───────────────────────────────────────────────────────────────
void USMPClient::keepalive(uint32_t ms) { _ctx.keepalive_ms = ms; }

void USMPClient::setLogLevel(usmp_log_level_t level) { usmp_set_log_level(level); }

// maintain
void USMPClient::maintain() {
  if (!_initialized) return;

  // Dead — attempt reconnect with exponential backoff
  if (!_ctx.established) {
    uint32_t now = millis();
    if (now - _last_attempt_ms < _backoff_ms) return;
    _last_attempt_ms = now;

    if (_do_reconnect()) {
      _backoff_ms = 2000;
      if (_on_reconnect) _on_reconnect();
      if (_on_connect) _on_connect();
    } else {
      if (_backoff_ms < 30000) _backoff_ms *= 2;
    }
    return;
  }

  // Alive — send keepalive PING if idle
  if (usmp_keepalive_tick(&_ctx) < 0) {
    _ctx.established = false;
    if (_on_disconnect) _on_disconnect();
    return;
  }

  // Non-blocking receive — fire onMessage if data waiting
  if (_on_message && available()) {
    int n = usmp_recv(&_ctx, _rx_buf, sizeof(_rx_buf));
    if (n > 0) {
      _on_message(_rx_buf, (size_t)n);
    } else if (n < 0) {
      _ctx.established = false;
      if (_on_disconnect) _on_disconnect();
    }
  }
}

// callbacks ─────────────────────────────────────────────────────────────────

void USMPClient::onConnect(void (*cb)()) { _on_connect = cb; }
void USMPClient::onDisconnect(void (*cb)()) { _on_disconnect = cb; }
void USMPClient::onReconnect(void (*cb)()) { _on_reconnect = cb; }
void USMPClient::onMessage(void (*cb)(const uint8_t*, size_t len)) { _on_message = cb; }

// manual control ─────────────────────────────────────────────────────────────

bool USMPClient::reconnect() {
  bool ok = _do_reconnect();
  if (ok) {
    if (_on_reconnect) _on_reconnect();
    if (_on_connect) _on_connect();
  }
  return ok;
}

void USMPClient::close() {
  usmp_close(&_ctx);
  if (_transport.destroy) {
    _transport.destroy(&_transport);
  }
  _initialized = false;
}
