#include "dxp_transport.h"
#include "dxp_port.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

// ── TCP transport implementation ──────────────────────────────────────────────

static int tcp_send(dxp_transport_t *t, const uint8_t *data, size_t len)
{
    int sock = (int)(intptr_t)t->ctx;
    return send(sock, data, len, 0) == (ssize_t)len ? 0 : -1;
}

static int tcp_recv(dxp_transport_t *t, uint8_t *buf, size_t max_len)
{
    int sock = (int)(intptr_t)t->ctx;
    return recv(sock, buf, max_len, 0);
}

static void tcp_close(dxp_transport_t *t)
{
    int sock = (int)(intptr_t)t->ctx;
    if (sock >= 0)
    {
        close(sock);
        t->ctx = (void *)(intptr_t)(-1);
    }
}

// ── Factory ───────────────────────────────────────────────────────────────────

int dxp_transport_tcp_init(dxp_transport_t *t, const char *server_ip, int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return -1;

    // TCP_NODELAY — disable Nagle for lower latency
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };

    if (inet_pton(AF_INET, server_ip, &addr.sin_addr) != 1)
    {
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        close(sock);
        return -1;
    }

    t->send = tcp_send;
    t->recv = tcp_recv;
    t->close = tcp_close;
    t->ctx = (void *)(intptr_t)sock;

    return 0;
}