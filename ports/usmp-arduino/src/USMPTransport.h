#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>

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
  USMPTCPTransport(const char* host, uint16_t port)
      : _host(host), _port(port), _ssid(nullptr), _password(nullptr) {}

  // Optional: let USMP manage WiFi — usmp.begin(USMP::TCP(...).wifi("SSID",
  // "pass"))
  USMPTCPTransport& wifi(const char* ssid, const char* password) {
    _ssid = ssid;
    _password = password;
    return *this;
  }

  bool connectWiFi() const;
  bool init(usmp_transport_t* t) const;

  const char* _host;
  uint16_t _port;
  const char* _ssid;
  const char* _password;
};

// Internal UDP context ──────────────────────────────────────────────────────
struct USMPArduinoUdpCtx {
  WiFiUDP udp;
  char host[64];
  uint16_t port;
  uint8_t rx_buf[USMP_HEADER_SIZE + USMP_MAX_PAYLOAD];
  int rx_len;
  uint32_t last_rx_seq;
  bool last_rx_seq_set;
  uint8_t last_rx_type;
  uint8_t tx_key[32];  // S3: authenticates ACKs we receive (peer signs with its rx_key)
  uint8_t rx_key[32];  // S3: signs ACKs we send for frames we received
  bool keys_set;
};

// UDP transport factory ─────────────────────────────────────────────────────
class USMPUDPTransport {
 public:
  USMPUDPTransport(const char* host, uint16_t port)
      : _host(host), _port(port), _ssid(nullptr), _password(nullptr) {}

  USMPUDPTransport& wifi(const char* ssid, const char* password) {
    _ssid = ssid;
    _password = password;
    return *this;
  }

  bool connectWiFi() const;
  bool init(usmp_transport_t* t) const;

  const char* _host;
  uint16_t _port;
  const char* _ssid;
  const char* _password;
};

// Ergonomic namespace: USMP::TCP("ip", port).wifi("ssid", "pass")
namespace USMP {
inline USMPTCPTransport TCP(const char* host, uint16_t port = 9000) {
  return USMPTCPTransport(host, port);
}
inline USMPUDPTransport UDP(const char* host, uint16_t port = 9000) {
  return USMPUDPTransport(host, port);
}
}  // namespace USMP