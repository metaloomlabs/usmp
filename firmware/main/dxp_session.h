#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "dxp_handshake.h"

typedef struct {
    dxp_session_t hs;        // handshake result (device_id, session_id, session_key)
    uint32_t      tx_seq;    // our outgoing sequence number
    uint32_t      rx_seq;    // last received sequence number from server
    int           sock;
} dxp_ctx_t;

// Send an encrypted DATA frame
int dxp_send(dxp_ctx_t *ctx, const uint8_t *data, uint16_t len);

// Receive and decrypt a DATA frame
int dxp_recv(dxp_ctx_t *ctx, uint8_t *out, uint16_t max_len);