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

// Shared transport parameters + WiFi bring-up ─────────────────────────────────
// Holds the connection params and the (identical for TCP/UDP) WiFi logic so the
// concrete transports only differ in init(). Never used polymorphically / never
// heap-deleted through this type — the derived objects are plain stack values,
// so no virtual destructor is needed.
class USMPTransportBase {
 public:
  USMPTransportBase(const char* host, uint16_t port)
      : _host(host), _port(port), _ssid(nullptr), _password(nullptr) {}

  // Connect STA WiFi using the stored credentials. Returns true immediately if
  // WiFi is managed externally (no SSID set). Shared by TCP and UDP.
  bool connectWiFi() const;

  const char* _host;
  uint16_t _port;
  const char* _ssid;
  const char* _password;

 protected:
  void _setWiFi(const char* ssid, const char* password) {
    _ssid = ssid;
    _password = password;
  }
};

// Internal TCP context ──────────────────────────────────────────────────────
// Allocated on heap in USMPTCPTransport::init(). Lives for transport lifetime.
struct USMPArduinoTcpCtx {
  WiFiClient client;
  char host[64];
  uint16_t port;
  bool session_active;  // false during handshake (recv unbounded), true once established
};

// TCP transport factory ─────────────────────────────────────────────────────
class USMPTCPTransport : public USMPTransportBase {
 public:
  USMPTCPTransport(const char* host, uint16_t port) : USMPTransportBase(host, port) {}

  // Optional: let USMP manage WiFi — usmp.begin(USMP::TCP(...).wifi("SSID", "pass"))
  // Returns the derived type so the fluent builder keeps its concrete static type.
  USMPTCPTransport& wifi(const char* ssid, const char* password) {
    _setWiFi(ssid, password);
    return *this;
  }

  bool init(usmp_transport_t* t) const;
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
class USMPUDPTransport : public USMPTransportBase {
 public:
  USMPUDPTransport(const char* host, uint16_t port) : USMPTransportBase(host, port) {}

  USMPUDPTransport& wifi(const char* ssid, const char* password) {
    _setWiFi(ssid, password);
    return *this;
  }

  bool init(usmp_transport_t* t) const;
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
