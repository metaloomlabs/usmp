#pragma once
#include "dxp.h"
#include "dxp_handshake.h"

// dxp_send and dxp_recv operate directly on dxp_t
// signatures match the public API in dxp.h
int dxp_send(dxp_t *ctx, const uint8_t *data, uint16_t len);
int dxp_recv(dxp_t *ctx, uint8_t *out, uint16_t max_len);