#include "dxp_transport.h"
#include "dxp_port.h"
#include "dxp_frame.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>

// ── TCP context ───────────────────────────────────────────────────────────────
// Allocated once in dxp_transport_tcp_init, lives for the lifetime of the
// transport. Not freed on close — allows reconnect without losing ip/port.
typedef struct
{
    int sock;
    char server_ip[64];
    int port;
} dxp_tcp_ctx_t;

// ── Internal helpers ──────────────────────────────────────────────────────────

static int tcp_dial(dxp_tcp_ctx_t *tcp)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return -1;

    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(tcp->port),
    };

    if (inet_pton(AF_INET, tcp->server_ip, &addr.sin_addr) != 1)
    {
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        close(sock);
        return -1;
    }

    tcp->sock = sock;
    return 0;
}

// ── Transport hooks ───────────────────────────────────────────────────────────

static int dxp_tcp_send(dxp_transport_t *t, const uint8_t *data, size_t len)
{
    dxp_tcp_ctx_t *tcp = (dxp_tcp_ctx_t *)t->ctx;
    size_t sent = 0;
    while (sent < len)
    {
        ssize_t n = send(tcp->sock, data + sent, len - sent, 0);
        if (n < 0)
            return -1;
        sent += n;
    }
    return 0;
}

static int dxp_tcp_recv(dxp_transport_t *t, uint8_t *buf, size_t max_len)
{
    dxp_tcp_ctx_t *tcp = (dxp_tcp_ctx_t *)t->ctx;

    // Step 1: read header exactly
    if (max_len < DXP_HEADER_SIZE)
        return -1;
    size_t received = 0;
    while (received < DXP_HEADER_SIZE)
    {
        ssize_t n = recv(tcp->sock, buf + received, DXP_HEADER_SIZE - received, 0);
        if (n <= 0)
            return -1;
        received += n;
    }

    // Step 2: parse payload length from header
    uint16_t payload_len = buf[8] | (buf[9] << 8);
    if (payload_len > DXP_MAX_PAYLOAD)
        return -1;
    if (DXP_HEADER_SIZE + payload_len > max_len)
        return -1;

    // Step 3: read payload exactly
    while (received < DXP_HEADER_SIZE + payload_len)
    {
        ssize_t n = recv(tcp->sock, buf + received,
                         DXP_HEADER_SIZE + payload_len - received, 0);
        if (n <= 0)
            return -1;
        received += n;
    }

    return (int)received;
}

static void dxp_tcp_close(dxp_transport_t *t)
{
    dxp_tcp_ctx_t *tcp = (dxp_tcp_ctx_t *)t->ctx;
    if (tcp && tcp->sock >= 0)
    {
        close(tcp->sock);
        tcp->sock = -1;
    }
    // ctx intentionally NOT freed — ip/port retained for reconnect
}

static int dxp_tcp_reconnect(dxp_transport_t *t)
{
    dxp_tcp_ctx_t *tcp = (dxp_tcp_ctx_t *)t->ctx;
    if (!tcp)
        return -1;

    // Close existing socket if still open
    if (tcp->sock >= 0)
    {
        close(tcp->sock);
        tcp->sock = -1;
    }

    return tcp_dial(tcp);
}

static int dxp_tcp_available(dxp_transport_t *t)
{
    dxp_tcp_ctx_t *tcp = (dxp_tcp_ctx_t *)t->ctx;
    if (!tcp || tcp->sock < 0)
        return 0;
    int count = 0;
    if (ioctl(tcp->sock, FIONREAD, &count) < 0)
        return 0;
    return count;
}

// ── Factory ───────────────────────────────────────────────────────────────────

int dxp_transport_tcp_init(dxp_transport_t *t, const char *server_ip, int port)
{
    dxp_tcp_ctx_t *tcp = (dxp_tcp_ctx_t *)malloc(sizeof(dxp_tcp_ctx_t));
    if (!tcp)
        return -1;

    tcp->sock = -1;
    tcp->port = port;
    strncpy(tcp->server_ip, server_ip, sizeof(tcp->server_ip) - 1);
    tcp->server_ip[sizeof(tcp->server_ip) - 1] = '\0';

    if (tcp_dial(tcp) != 0)
    {
        free(tcp);
        return -1;
    }

    t->send = dxp_tcp_send;
    t->recv = dxp_tcp_recv;
    t->close = dxp_tcp_close;
    t->reconnect = dxp_tcp_reconnect;
    t->available = dxp_tcp_available;
    t->ctx = tcp;

    return 0;
}