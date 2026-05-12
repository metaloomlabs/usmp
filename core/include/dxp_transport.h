#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct dxp_transport_s
    {
        int (*send)(struct dxp_transport_s *t, const uint8_t *data, size_t len);
        int (*recv)(struct dxp_transport_s *t, uint8_t *buf, size_t max_len);
        void (*close)(struct dxp_transport_s *t);
        int (*reconnect)(struct dxp_transport_s *t);
        int (*available)(struct dxp_transport_s *t); // ← new: bytes waiting, 0=none, NULL=unsupported
        void *ctx;
    } dxp_transport_t;

    int dxp_transport_tcp_init(dxp_transport_t *t, const char *server_ip, int port);

#ifdef __cplusplus
}
#endif