#pragma once
#include <Arduino.h>

#include "USMPTransport.h"

extern "C" {
#include "usmp_api.h"
}

class USMPClient : public Print {
 public:
  explicit USMPClient(const char* psk);
  USMPClient(const char* psk, uint8_t* rx_buffer, size_t rx_buffer_size);
  USMPClient(const uint8_t* psk, size_t psk_len);
  USMPClient(const uint8_t* psk, size_t psk_len, uint8_t* rx_buffer, size_t rx_buffer_size);
  virtual ~USMPClient();
  bool begin(USMPTCPTransport transport, USMPArduinoTcpCtx* static_ctx = nullptr);
  bool begin(USMPUDPTransport transport, USMPArduinoUdpCtx* static_ctx = nullptr);
  bool beginAsync(USMPTCPTransport transport, USMPArduinoTcpCtx* static_ctx = nullptr);
  bool beginAsync(USMPUDPTransport transport, USMPArduinoUdpCtx* static_ctx = nullptr);
  usmp_state_t state() const { return usmp_get_state(&_ctx); }
  bool isConnecting() const {
    usmp_state_t s = state();
    return s > USMP_STATE_IDLE && s < USMP_STATE_ESTABLISHED;
  }
  usmp_err_t lastError() const { return _last_err; }
  const char* lastErrorString() const { return usmp_strerror(_last_err); }
  bool send(const char* str);
  bool send(const String& str);
  bool send(const uint8_t* data, size_t len);

  // Print interface implementation (enables usmp.print() and usmp.println())
  virtual size_t write(uint8_t c) override;
  virtual size_t write(const uint8_t* buffer, size_t size) override;
  virtual void flush() override;
  virtual int availableForWrite() override { return (int)(USMP_MAX_DATA_LEN - _tx_len); }

  bool available();
  String read();
  int read(uint8_t* buf, size_t max_len);
  bool alive();
  String deviceId();
  String sessionId();
  void keepalive(uint32_t ms);
  void setLogLevel(usmp_log_level_t level);
  void setHandshakeScratch(uint8_t* scratch, size_t len);
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
  const uint8_t* _psk_bytes;
  size_t _psk_len;
  usmp_err_t _last_err;
  uint8_t _tx_buf[USMP_MAX_DATA_LEN];
  size_t _tx_len;
  usmp_t _ctx;
  usmp_transport_t _transport;
  bool _initialized;
  bool _reconnecting;
  uint32_t _backoff_ms;
  uint32_t _last_attempt_ms;
  void (*_on_connect)();
  void (*_on_disconnect)();
  void (*_on_reconnect)();
  void (*_on_message)(const uint8_t* data, size_t len);
  uint8_t* _rx_buf;
  size_t _rx_buf_capacity;
  bool _owns_rx_buf;
  size_t _rx_len;
  void _apply_psk();
  bool _do_reconnect();
  void _drain_rx();

  // Level-gated Serial log helper — single place the log threshold is checked.
  void _logf(usmp_log_level_t level, const char* fmt, ...);

  // Shared body of the begin() and beginAsync() overloads. `Transport` is USMPTCPTransport or
  // USMPUDPTransport; they differ only in connectWiFi()/init() and the proto tag.
  // Defined in USMP.cpp (both instantiations live in that TU).
  template <typename Transport, typename CtxType>
  bool _beginImpl(const Transport& transport, const char* proto, CtxType* static_ctx, bool async_mode = false);
};

/**
 * Zero-heap client wrapper with embedded static RX and handshake scratch buffers.
 * Suitable for microcontrollers without heap or strict zero-fragmentation requirements.
 */
template <size_t RxBufferSize = USMP_MAX_DATA_LEN * USMP_MAX_FRAMES,
          size_t ScratchBufferSize = USMP_HANDSHAKE_SCRATCH_LEN>
class USMPClientStatic : public USMPClient {
 public:
  explicit USMPClientStatic(const char* psk)
      : USMPClient(psk, _embedded_rx_buf, RxBufferSize) {
    setHandshakeScratch(_embedded_scratch, ScratchBufferSize);
  }

  USMPClientStatic(const uint8_t* psk, size_t psk_len)
      : USMPClient(psk, psk_len, _embedded_rx_buf, RxBufferSize) {
    setHandshakeScratch(_embedded_scratch, ScratchBufferSize);
  }

 private:
  uint8_t _embedded_rx_buf[RxBufferSize];
  uint8_t _embedded_scratch[ScratchBufferSize];
};