#include "USMP.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

USMPClient::USMPClient(const char* psk)
    : _psk(psk),
      _psk_bytes((const uint8_t*)psk),
      _psk_len(psk ? strlen(psk) : 0),
      _last_err(USMP_OK),
      _tx_len(0),
      _initialized(false),
      _reconnecting(false),
      _backoff_ms(2000),
      _last_attempt_ms(0),
      _on_connect(nullptr),
      _on_disconnect(nullptr),
      _on_reconnect(nullptr),
      _on_message(nullptr),
      _rx_buf(nullptr),
      _rx_buf_capacity(USMP_MAX_DATA_LEN * USMP_MAX_FRAMES),
      _owns_rx_buf(true),
      _rx_len(0) {
  memset(_tx_buf, 0, sizeof(_tx_buf));
  _rx_buf = new uint8_t[_rx_buf_capacity];
  if (_rx_buf) {
    memset(_rx_buf, 0, _rx_buf_capacity);
  }
  memset(&_ctx, 0, sizeof(_ctx));
  memset(&_transport, 0, sizeof(_transport));
}

USMPClient::USMPClient(const char* psk, uint8_t* rx_buffer, size_t rx_buffer_size)
    : _psk(psk),
      _psk_bytes((const uint8_t*)psk),
      _psk_len(psk ? strlen(psk) : 0),
      _last_err(USMP_OK),
      _tx_len(0),
      _initialized(false),
      _reconnecting(false),
      _backoff_ms(2000),
      _last_attempt_ms(0),
      _on_connect(nullptr),
      _on_disconnect(nullptr),
      _on_reconnect(nullptr),
      _on_message(nullptr),
      _rx_buf(rx_buffer),
      _rx_buf_capacity(rx_buffer_size),
      _owns_rx_buf(false),
      _rx_len(0) {
  memset(_tx_buf, 0, sizeof(_tx_buf));
  if (_rx_buf && _rx_buf_capacity > 0) {
    memset(_rx_buf, 0, _rx_buf_capacity);
  }
  memset(&_ctx, 0, sizeof(_ctx));
  memset(&_transport, 0, sizeof(_transport));
}

USMPClient::USMPClient(const uint8_t* psk, size_t psk_len)
    : _psk(nullptr),
      _psk_bytes(psk),
      _psk_len(psk_len),
      _last_err(USMP_OK),
      _tx_len(0),
      _initialized(false),
      _reconnecting(false),
      _backoff_ms(2000),
      _last_attempt_ms(0),
      _on_connect(nullptr),
      _on_disconnect(nullptr),
      _on_reconnect(nullptr),
      _on_message(nullptr),
      _rx_buf(nullptr),
      _rx_buf_capacity(USMP_MAX_DATA_LEN * USMP_MAX_FRAMES),
      _owns_rx_buf(true),
      _rx_len(0) {
  memset(_tx_buf, 0, sizeof(_tx_buf));
  _rx_buf = new uint8_t[_rx_buf_capacity];
  if (_rx_buf) {
    memset(_rx_buf, 0, _rx_buf_capacity);
  }
  memset(&_ctx, 0, sizeof(_ctx));
  memset(&_transport, 0, sizeof(_transport));
}

USMPClient::USMPClient(const uint8_t* psk, size_t psk_len, uint8_t* rx_buffer, size_t rx_buffer_size)
    : _psk(nullptr),
      _psk_bytes(psk),
      _psk_len(psk_len),
      _last_err(USMP_OK),
      _tx_len(0),
      _initialized(false),
      _reconnecting(false),
      _backoff_ms(2000),
      _last_attempt_ms(0),
      _on_connect(nullptr),
      _on_disconnect(nullptr),
      _on_reconnect(nullptr),
      _on_message(nullptr),
      _rx_buf(rx_buffer),
      _rx_buf_capacity(rx_buffer_size),
      _owns_rx_buf(false),
      _rx_len(0) {
  memset(_tx_buf, 0, sizeof(_tx_buf));
  if (_rx_buf && _rx_buf_capacity > 0) {
    memset(_rx_buf, 0, _rx_buf_capacity);
  }
  memset(&_ctx, 0, sizeof(_ctx));
  memset(&_transport, 0, sizeof(_transport));
}

USMPClient::~USMPClient() {
  close();
  if (_owns_rx_buf && _rx_buf) {
    delete[] _rx_buf;
    _rx_buf = nullptr;
  }
}

// Internal helpers ──────────────────────────────────────────────────────────

void USMPClient::setHandshakeScratch(uint8_t* scratch, size_t len) {
  _ctx.scratch = scratch;
  _ctx.scratch_len = len;
}

void USMPClient::_apply_psk() {
  _ctx.psk = _psk_bytes;
  _ctx.psk_len = _psk_len;
}

void USMPClient::_logf(usmp_log_level_t level, const char* fmt, ...) {
  if (usmp_get_log_level() < level) return;
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.println(buf);
}

bool USMPClient::_do_reconnect() {
  _apply_psk();
  _last_err = usmp_reconnect(&_ctx);
  return _last_err == USMP_OK;
}

void USMPClient::_drain_rx() {
  if (!_ctx.established || _rx_len > 0) return;
  if (!_transport.available || !_rx_buf || _rx_buf_capacity == 0) return;

  while (_ctx.established && _rx_len == 0 && _transport.available(&_transport) > 0) {
    int n = usmp_recv(&_ctx, _rx_buf, (uint16_t)_rx_buf_capacity);
    if (n > 0) {
      _rx_len = (size_t)n;
      _last_err = USMP_OK;
      break;
    } else if (n < 0) {
      _last_err = (usmp_err_t)n;
      _ctx.established = false;
      if (_on_disconnect) _on_disconnect();
      break;
    }
  }
}

// begin ─────────────────────────────────────────────────────────────────────

template <typename Transport, typename CtxType>
bool USMPClient::_beginImpl(const Transport& transport, const char* proto, CtxType* static_ctx, bool async_mode) {
  // WiFi — only if USMP is managing it (SSID was supplied via .wifi()).
  if (transport._ssid) {
    _logf(USMP_LOG_LEVEL_INFO, "[USMP] Connecting to WiFi: %s", transport._ssid);
    if (!transport.connectWiFi()) {
      _last_err = USMP_ERR_TRANSPORT_FAILED;
      _logf(USMP_LOG_LEVEL_ERROR, "[usmp]: WiFi connect failed");
      return false;
    }
    _logf(USMP_LOG_LEVEL_INFO, "[USMP] WiFi connected — IP: %s",
          WiFi.localIP().toString().c_str());
  }

  // Transport init ─────────────────────────────────────────────────────────
  memset(&_transport, 0, sizeof(_transport));
  bool init_ok = false;
  if (static_ctx) {
    init_ok = transport.init_static(&_transport, static_ctx);
  } else {
    init_ok = transport.init(&_transport);
  }

  if (!init_ok) {
    _last_err = USMP_ERR_TRANSPORT_FAILED;
    _logf(USMP_LOG_LEVEL_ERROR, "[usmp]: %s connect failed", proto);
    return false;
  }

  // USMP handshake ─────────────────────────────────────────────────────────
  uint8_t* scratch = _ctx.scratch;
  size_t scratch_len = _ctx.scratch_len;
  memset(&_ctx, 0, sizeof(_ctx));
  _apply_psk();
  _ctx.keepalive_ms = 30000;  // 30s default
  _ctx.scratch = scratch;
  _ctx.scratch_len = scratch_len;

  if (async_mode) {
    _last_err = usmp_connect_async(&_ctx, &_transport);
    if (_last_err != USMP_OK) {
      _logf(USMP_LOG_LEVEL_ERROR, "[usmp]: Async connect init failed");
      return false;
    }
  } else {
    _last_err = usmp_connect(&_ctx, &_transport);
    if (_last_err != USMP_OK) {
      _logf(USMP_LOG_LEVEL_ERROR, "[usmp]: Handshake failed");
      return false;
    }
  }

  _last_err = USMP_OK;
  _initialized = true;
  _reconnecting = false;
  _backoff_ms = 2000;
  _last_attempt_ms = 0;
  _rx_len = 0;

  if (!async_mode && _on_connect) _on_connect();
  return true;
}

bool USMPClient::begin(USMPTCPTransport transport, USMPArduinoTcpCtx* static_ctx) {
  return _beginImpl(transport, "TCP", static_ctx, false);
}

bool USMPClient::begin(USMPUDPTransport transport, USMPArduinoUdpCtx* static_ctx) {
  return _beginImpl(transport, "UDP", static_ctx, false);
}

bool USMPClient::beginAsync(USMPTCPTransport transport, USMPArduinoTcpCtx* static_ctx) {
  return _beginImpl(transport, "TCP", static_ctx, true);
}

bool USMPClient::beginAsync(USMPUDPTransport transport, USMPArduinoUdpCtx* static_ctx) {
  return _beginImpl(transport, "UDP", static_ctx, true);
}

// send ──────────────────────────────────────────────────────────────────────

bool USMPClient::send(const char* str) { return send((const uint8_t*)str, strlen(str)); }

bool USMPClient::send(const String& str) { return send((const uint8_t*)str.c_str(), str.length()); }

bool USMPClient::send(const uint8_t* data, size_t len) {
  if (_tx_len > 0) {
    flush();
  }
  if (!_ctx.established) {
    _last_err = USMP_ERR_NOT_CONNECTED;
    return false;
  }
  _last_err = usmp_send(&_ctx, data, (uint16_t)len);
  if (_last_err != USMP_OK) {
    _ctx.established = false;
    if (_on_disconnect) _on_disconnect();
    return false;
  }
  return true;
}

// Print interface ───────────────────────────────────────────────────────────

size_t USMPClient::write(uint8_t c) {
  if (!_ctx.established) {
    _last_err = USMP_ERR_NOT_CONNECTED;
    return 0;
  }
  if (_tx_len >= USMP_MAX_DATA_LEN) {
    flush();
  }
  _tx_buf[_tx_len++] = c;
  if (c == '\n') {
    flush();
  }
  return 1;
}

size_t USMPClient::write(const uint8_t* buffer, size_t size) {
  if (!buffer || size == 0) return 0;
  if (!_ctx.established) {
    _last_err = USMP_ERR_NOT_CONNECTED;
    return 0;
  }

  size_t written = 0;
  while (written < size) {
    if (_tx_len >= USMP_MAX_DATA_LEN) {
      flush();
    }
    size_t space = USMP_MAX_DATA_LEN - _tx_len;
    size_t to_copy = (size - written < space) ? (size - written) : space;

    const uint8_t* nl = (const uint8_t*)memchr(buffer + written, '\n', to_copy);
    if (nl) {
      size_t chunk = (size_t)(nl - (buffer + written)) + 1;
      memcpy(_tx_buf + _tx_len, buffer + written, chunk);
      _tx_len += chunk;
      written += chunk;
      flush();
    } else {
      memcpy(_tx_buf + _tx_len, buffer + written, to_copy);
      _tx_len += to_copy;
      written += to_copy;
      if (_tx_len >= USMP_MAX_DATA_LEN) {
        flush();
      }
    }
  }
  return written;
}

void USMPClient::flush() {
  if (_tx_len > 0 && _ctx.established) {
    size_t len = _tx_len;
    _tx_len = 0;
    _last_err = usmp_send(&_ctx, _tx_buf, (uint16_t)len);
    if (_last_err != USMP_OK) {
      _ctx.established = false;
      if (_on_disconnect) _on_disconnect();
    }
  } else {
    _tx_len = 0;
  }
}

// receive ───────────────────────────────────────────────────────────────────

bool USMPClient::available() {
  _drain_rx();
  return _rx_len > 0;
}

String USMPClient::read() {
  _drain_rx();
  if (_rx_len == 0 || !_rx_buf) {
    return String();
  }
  String msg((char*)_rx_buf, _rx_len);
  _rx_len = 0;
  return msg;
}

int USMPClient::read(uint8_t* buf, size_t max_len) {
  _drain_rx();
  if (_rx_len == 0 || !_rx_buf) {
    return 0;
  }
  size_t to_copy = (_rx_len < max_len) ? _rx_len : max_len;
  memcpy(buf, _rx_buf, to_copy);
  _rx_len = 0;
  return (int)to_copy;
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

  // Non-blocking handshake in progress (e.g. from beginAsync or async reconnect)
  if (isConnecting()) {
    usmp_err_t err = usmp_step(&_ctx);
    _last_err = err;
    if (err == USMP_OK && _ctx.established) {
      _backoff_ms = 2000;
      if (_reconnecting && _on_reconnect) _on_reconnect();
      _reconnecting = false;
      if (_on_connect) _on_connect();
    } else if (err != USMP_OK) {
      _reconnecting = false;
      if (_backoff_ms < 30000) _backoff_ms *= 2;
    }
    return;
  }

  // Dead — attempt reconnect with exponential backoff asynchronously
  if (!_ctx.established) {
    uint32_t now = millis();
    if (now - _last_attempt_ms < _backoff_ms) return;
    _last_attempt_ms = now;

    _reconnecting = true;
    _apply_psk();
    _last_err = usmp_reconnect_async(&_ctx);
    if (_last_err != USMP_OK) {
      _reconnecting = false;
      if (_backoff_ms < 30000) _backoff_ms *= 2;
    }
    return;
  }

  // Alive — send keepalive PING if idle
  usmp_err_t step_err = usmp_step(&_ctx);
  if (step_err != USMP_OK) {
    _last_err = step_err;
    _ctx.established = false;
    if (_on_disconnect) _on_disconnect();
    return;
  }

  // Non-blocking receive — drain control frames into background & buffer application data
  _drain_rx();

  // Fire onMessage if application data is buffered
  if (_on_message && _rx_len > 0 && _rx_buf) {
    size_t len = _rx_len;
    _rx_len = 0;
    _on_message(_rx_buf, len);
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
  _tx_len = 0;
  usmp_close(&_ctx);
  if (_transport.destroy) {
    _transport.destroy(&_transport);
  }
  _rx_len = 0;
  _initialized = false;
  _reconnecting = false;
  _last_err = USMP_OK;
}
