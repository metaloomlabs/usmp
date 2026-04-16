#pragma once
#include <stdint.h>

int dxp_tcp_connect(const char *ip, int port);
int dxp_tcp_send(int sock, const uint8_t *data, int len);
int dxp_tcp_recv(int sock, uint8_t *buffer, int max_len);