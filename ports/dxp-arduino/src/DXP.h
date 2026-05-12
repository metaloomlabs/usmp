#pragma once
#include <Arduino.h>
#include "DXPTransport.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "dxp_core/include/dxp.h"
#ifdef __cplusplus
}
#endif

class DXPClient {
public:
    // ── Constructor ───────────────────────────────────────────────────────────
    explicit DXPClient(const char *psk);

    // ── Connect ───────────────────────────────────────────────────────────────
    // Pass a transport factory: DXP::TCP("ip").wifi("ssid", "pass")
    bool begin(DXPTCPTransport transport);

    // ── Send ──────────────────────────────────────────────────────────────────
    bool send(const char *str);
    bool send(const String &str);
    bool send(const uint8_t *data, size_t len);

    // ── Receive ───────────────────────────────────────────────────────────────
    bool    available();                         // true if data is waiting (non-blocking)
    String  read();                              // blocking — returns next message as String
    int     read(uint8_t *buf, size_t max_len);  // blocking — returns bytes read, -1 on error

    // ── State ─────────────────────────────────────────────────────────────────
    bool    alive();        // true if session is established
    String  deviceId();     // "00:70:07:2d:42:24"
    String  sessionId();    // "afab10bf"

    // ── Keepalive ─────────────────────────────────────────────────────────────
    void    keepalive(uint32_t ms);   // PING interval (default: 30s, 0 = disabled)

    // ── Main loop driver ──────────────────────────────────────────────────────
    // Call in loop(). Handles: PING keepalive, dead socket detection,
    // reconnect with backoff, onMessage callback for incoming data.
    void    maintain();

    // ── Callbacks ─────────────────────────────────────────────────────────────
    void    onConnect   (void (*cb)());
    void    onDisconnect(void (*cb)());
    void    onReconnect (void (*cb)());
    void    onMessage   (void (*cb)(const uint8_t *data, size_t len));

    // ── Manual control (advanced) ─────────────────────────────────────────────
    bool    reconnect();
    void    close();

private:
    const char          *_psk;
    dxp_t                _ctx;
    dxp_transport_t      _transport;
    bool                 _initialized;

    uint32_t             _backoff_ms;
    uint32_t             _last_attempt_ms;

    void (*_on_connect)   ();
    void (*_on_disconnect)();
    void (*_on_reconnect) ();
    void (*_on_message)   (const uint8_t *data, size_t len);

    uint8_t _rx_buf[DXP_MAX_DATA_LEN];

    void _apply_psk();
    bool _do_reconnect();
};