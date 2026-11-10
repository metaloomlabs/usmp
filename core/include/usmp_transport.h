#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "usmp_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct usmp_transport_s {
  int (*send)(struct usmp_transport_s* t, const uint8_t* data, size_t len);
  int (*recv)(struct usmp_transport_s* t, uint8_t* buf, size_t max_len);
  void (*close)(struct usmp_transport_s* t);
  int (*reconnect)(struct usmp_transport_s* t);
  int (*available)(struct usmp_transport_s* t);  // bytes waiting, 0=none, NULL=unsupported
  void (*destroy)(struct usmp_transport_s* t);
  void (*confirm_authenticated)(struct usmp_transport_s* t, uint32_t seq);
  /* S3: install the derived directional session keys (32 bytes each) so a UDP transport
     can authenticate session-phase UTACKs. NULL for transports that don't need it (TCP). */
  void (*set_session_keys)(struct usmp_transport_s* t, const uint8_t* tx_key,
                           const uint8_t* rx_key);
  void* ctx;
} usmp_transport_t;

/* ── Static Context Definitions for Zero-Heap Transports ── */

typedef struct {
  int sock;
  char server_ip[64];
  int port;
  bool session_active;
} usmp_tcp_ctx_t;

typedef struct {
  int sock;
  char server_ip[64];
  int port;
  uint8_t rx_buf[USMP_HEADER_SIZE + USMP_MAX_PAYLOAD];
  int rx_len;
  uint32_t last_rx_seq;
  bool last_rx_seq_set;
  uint8_t last_rx_type;
  uint8_t tx_key[32];  // S3: authenticates ACKs we receive (peer signs with its rx_key)
  uint8_t rx_key[32];  // S3: signs ACKs we send for frames we received
  bool keys_set;
  uint32_t srtt;
  uint32_t rttvar;
  uint32_t rto;
} usmp_udp_ctx_t;

/* ── Dynamic Initializers (allocates transport ctx on heap via malloc) ── */
int usmp_transport_tcp_init(usmp_transport_t* t, const char* server_ip, int port);
int usmp_transport_udp_init(usmp_transport_t* t, const char* server_ip, int port);

/* ── Zero-Heap Static Initializers (uses caller-managed static/BSS memory) ── */
int usmp_transport_tcp_init_static(usmp_transport_t* t, usmp_tcp_ctx_t* ctx,
                                   const char* server_ip, int port);
int usmp_transport_udp_init_static(usmp_transport_t* t, usmp_udp_ctx_t* ctx,
                                   const char* server_ip, int port);

#ifdef __cplusplus
}
#endif