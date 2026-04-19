#pragma once
#include <stdint.h>
#include <stdbool.h>

#define DXP_DEVICE_ID_LEN 6
#define DXP_NONCE_LEN 32
#define DXP_HMAC_LEN 32
#define DXP_SESSION_ID_LEN 4
#define DXP_SESSION_KEY_LEN 32 // AES-256 key

typedef struct
{
    uint8_t device_id[DXP_DEVICE_ID_LEN];
    uint8_t session_id[DXP_SESSION_ID_LEN];
    uint8_t session_key[DXP_SESSION_KEY_LEN]; // derived via HKDF
    bool established;
} dxp_session_t;

int dxp_handshake(int sock, dxp_session_t *session);