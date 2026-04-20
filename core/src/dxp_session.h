#pragma once
#include "dxp.h"

int dxp_send(dxp_t *ctx, const uint8_t *data, uint16_t len);
int dxp_recv(dxp_t *ctx, uint8_t *out, uint16_t max_len);