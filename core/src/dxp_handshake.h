#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "dxp.h"

#define DXP_NONCE_LEN 32
#define DXP_HMAC_LEN 32

typedef struct
{
    uint8_t device_id[DXP_DEVICE_ID_LEN];
    uint8_t session_id[DXP_SESSION_ID_LEN];
    uint8_t session_key[DXP_SESSION_KEY_LEN];
    bool established;
} dxp_session_t;

int dxp_handshake(dxp_transport_t *transport, dxp_session_t *session);