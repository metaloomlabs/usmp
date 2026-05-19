#pragma once
#include <Arduino.h>
#include "DXPTransport.h"

extern "C" {
#include "dxp_api.h"
}

class DXPClient {
public:
    explicit DXPClient(const char *psk);
    bool begin(DXPTCPTransport transport);
    bool send(const char *str);
    bool send(const String &str);
    bool send(const uint8_t *data, size_t len);
    bool available();
    String read();
    int read(uint8_t *buf, size_t max_len);
    bool alive();
    String deviceId();
    String sessionId();
    void keepalive(uint32_t ms);
    void maintain();
    void onConnect(void (*cb)());
    void onDisconnect(void (*cb)());
    void onReconnect(void (*cb)());
    void onMessage(void (*cb)(const uint8_t *data, size_t len));
    bool reconnect();
    void close();
private:
    const char         *_psk;
    dxp_t               _ctx;
    dxp_transport_t     _transport;
    bool                _initialized;
    uint32_t            _backoff_ms;
    uint32_t            _last_attempt_ms;
    void (*_on_connect)();
    void (*_on_disconnect)();
    void (*_on_reconnect)();
    void (*_on_message)(const uint8_t *data, size_t len);
    uint8_t _rx_buf[DXP_MAX_DATA_LEN];
    void _apply_psk();
    bool _do_reconnect();
};