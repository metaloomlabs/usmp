#pragma once

#include "dxp.h"
#include "dxp_transport.h"

#ifdef __cplusplus
extern "C"
{
#endif

// ── Handshake constants ───────────────────────────────────────────────────────
#define DXP_NONCE_LEN 32 // bytes — server challenge nonce
#define DXP_HMAC_LEN 32  // bytes — HMAC-SHA256 output

    /**
     * Perform DXP mutual-auth handshake over the given transport.
     * Populates session->device_id, session_id, session_key, established.
     * Returns 0 on success, -1 on failure.
     */
    int dxp_handshake(dxp_transport_t *transport, dxp_t *session);

#ifdef __cplusplus
}
#endif