#include "dxp_transport.h"
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

int dxp_tcp_connect(const char *ip, int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        return -1;
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };

    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) != 1)
    {
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0)
    {
        close(sock);
        return -1;
    }

    return sock;
}

int dxp_tcp_send(int sock, const uint8_t *data, int len)
{
    return send(sock, data, len, 0);
}

int dxp_tcp_recv(int sock, uint8_t *buffer, int max_len)
{
    return recv(sock, buffer, max_len, 0);
}