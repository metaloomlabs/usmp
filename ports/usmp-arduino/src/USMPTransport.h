#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>


#ifdef __cplusplus
extern "C" {
#endif
#include "usmp_frame.h"
#include "usmp_transport.h"

#ifdef __cplusplus
}
#endif

// Internal TCP context ──────────────────────────────────────────────────────
// Allocated on heap in USMPTCPTransport::init(). Lives for transport lifetime.
struct USMPArduinoTcpCtx {
  WiFiClient client;
  char host[64];
  uint16_t port;
};

// TCP transport factory ─────────────────────────────────────────────────────
class USMPTCPTransport {
public:
  USMPTCPTransport(const char *host, uint16_t port)
      : _host(host), _port(port), _ssid(nullptr), _password(nullptr) {}

  // Optional: let USMP manage WiFi — usmp.begin(USMP::TCP(...).wifi("SSID",
  // "pass"))
  USMPTCPTransport &wifi(const char *ssid, const char *password) {
    _ssid = ssid;
    _password = password;
    return *this;
  }

  bool connectWiFi() const;
  bool init(usmp_transport_t *t) const;

  const char *_host;
  uint16_t _port;
  const char *_ssid;
  const char *_password;
};

// Ergonomic namespace: USMP::TCP("ip", port).wifi("ssid", "pass")
namespace USMP {
inline USMPTCPTransport TCP(const char *host, uint16_t port = 9000) {
  return USMPTCPTransport(host, port);
}
} // namespace USMP