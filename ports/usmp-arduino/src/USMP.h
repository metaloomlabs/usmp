#pragma once
#include <Arduino.h>

#include "USMPTransport.h"

extern "C" {
#include "usmp_api.h"
}

class USMPClient {
 public:
  explicit USMPClient(const char* psk);
  ~USMPClient();
  bool begin(USMPTCPTransport transport);
  bool begin(USMPUDPTransport transport);
  bool send(const char* str);
  bool send(const String& str);
  bool send(const uint8_t* data, size_t len);
  bool available();
  String read();
  int read(uint8_t* buf, size_t max_len);
  bool alive();
  String deviceId();
  String sessionId();
  void keepalive(uint32_t ms);
  void setLogLevel(usmp_log_level_t level);
  void maintain();

  /*
   * Connection callbacks. Firing semantics (important):
   *   onConnect    — fires on EVERY session establishment: the initial begin()
   *                  AND every successful reconnect. Use it for "I have a live
   *                  session now" work (e.g. re-announce state to the server).
   *   onReconnect  — fires ADDITIONALLY (before onConnect) on a reconnect only.
   *                  Use it for reconnect-specific work.
   *   onDisconnect — fires when a live session is lost (send/recv/keepalive
   *                  failure, or peer BYE).
   *   onMessage    — fires from maintain() when application data arrives.
   * So a reconnect invokes BOTH onReconnect and onConnect. If you put send()s in
   * both, a reconnect sends both — that is intentional, not a bug.
   */
  void onConnect(void (*cb)());
  void onDisconnect(void (*cb)());
  void onReconnect(void (*cb)());
  void onMessage(void (*cb)(const uint8_t* data, size_t len));
  bool reconnect();
  void close();

 private:
  const char* _psk;
  usmp_t _ctx;
  usmp_transport_t _transport;
  bool _initialized;
  uint32_t _backoff_ms;
  uint32_t _last_attempt_ms;
  void (*_on_connect)();
  void (*_on_disconnect)();
  void (*_on_reconnect)();
  void (*_on_message)(const uint8_t* data, size_t len);
  uint8_t _rx_buf[USMP_MAX_DATA_LEN * USMP_MAX_FRAMES];
  size_t _rx_len;
  void _apply_psk();
  bool _do_reconnect();
  void _drain_rx();

  // Level-gated Serial log helper — single place the log threshold is checked.
  void _logf(usmp_log_level_t level, const char* fmt, ...);

  // Shared body of the two begin() overloads. `Transport` is USMPTCPTransport or
  // USMPUDPTransport; they differ only in connectWiFi()/init() and the proto tag.
  // Defined in USMP.cpp (both instantiations live in that TU).
  template <typename Transport>
  bool _beginImpl(const Transport& transport, const char* proto);
};