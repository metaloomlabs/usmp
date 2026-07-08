#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct usmp_transport_s {
  int (*send)(struct usmp_transport_s* t, const uint8_t* data, size_t len);
  int (*recv)(struct usmp_transport_s* t, uint8_t* buf, size_t max_len);
  void (*close)(struct usmp_transport_s* t);
  int (*reconnect)(struct usmp_transport_s* t);
  int (*available)(struct usmp_transport_s* t);  // ← new: bytes waiting, 0=none, NULL=unsupported
  void (*destroy)(struct usmp_transport_s* t);
  void (*confirm_authenticated)(struct usmp_transport_s* t, uint32_t seq);
  /* S3: install the derived directional session keys (32 bytes each) so a UDP transport
     can authenticate session-phase UTACKs. NULL for transports that don't need it (TCP). */
  void (*set_session_keys)(struct usmp_transport_s* t, const uint8_t* tx_key,
                           const uint8_t* rx_key);
  void* ctx;
} usmp_transport_t;

int usmp_transport_tcp_init(usmp_transport_t* t, const char* server_ip, int port);
int usmp_transport_udp_init(usmp_transport_t* t, const char* server_ip, int port);

#ifdef __cplusplus
}
#endif