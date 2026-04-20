#pragma once

#include "dxp.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Initialize a TCP transport and connect to server_ip:port.
     * Fills in t->send, t->recv, t->close, t->ctx.
     * Returns 0 on success, -1 on failure.
     */
    int dxp_transport_tcp_init(dxp_transport_t *t, const char *server_ip, int port);

    /**
     * Initialize a UART transport.
     * Returns 0 on success, -1 on failure.
     */
    // int dxp_transport_uart_init(dxp_transport_t *t, int uart_num, int baud_rate);

#ifdef __cplusplus
}
#endif