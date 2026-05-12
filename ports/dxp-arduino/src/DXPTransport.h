#pragma once
#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFi.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "dxp_core/include/dxp_transport.h"
#include "dxp_core/include/dxp_frame.h"
#ifdef __cplusplus
}
#endif

// ── Internal TCP context ──────────────────────────────────────────────────────
// Allocated on heap in DXPTCPTransport::init(). Lives for transport lifetime.
struct DXPArduinoTcpCtx {
    WiFiClient  client;
    char        host[64];
    uint16_t    port;
};

// ── TCP transport factory ─────────────────────────────────────────────────────
class DXPTCPTransport {
public:
    DXPTCPTransport(const char *host, uint16_t port)
        : _host(host), _port(port), _ssid(nullptr), _password(nullptr) {}

    // Optional: let DXP manage WiFi — dxp.begin(DXP::TCP(...).wifi("SSID", "pass"))
    DXPTCPTransport &wifi(const char *ssid, const char *password) {
        _ssid     = ssid;
        _password = password;
        return *this;
    }

    bool connectWiFi() const;
    bool init(dxp_transport_t *t) const;

    const char *_host;
    uint16_t    _port;
    const char *_ssid;
    const char *_password;
};

// ── Ergonomic namespace: DXP::TCP("ip", port).wifi("ssid", "pass") ────────────
namespace DXP {
    inline DXPTCPTransport TCP(const char *host, uint16_t port = 9000) {
        return DXPTCPTransport(host, port);
    }
}