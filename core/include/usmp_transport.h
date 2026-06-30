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
  void* ctx;
} usmp_transport_t;

int usmp_transport_tcp_init(usmp_transport_t* t, const char* server_ip, int port);
int usmp_transport_udp_init(usmp_transport_t* t, const char* server_ip, int port);

#ifdef __cplusplus
}
#endif