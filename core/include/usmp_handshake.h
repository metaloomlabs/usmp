#pragma once

#include "usmp.h"
#include "usmp_transport.h"

#ifdef __cplusplus
extern "C"
{
#endif

// ── Handshake constants ───────────────────────────────────────────────────────
#define USMP_NONCE_LEN 32 // bytes — server challenge nonce
#define USMP_HMAC_LEN 32  // bytes — HMAC-SHA256 output

    /**
     * Perform USMP mutual-auth handshake over the given transport.
     * Populates session->device_id, session_id, session_key, established.
     * Returns 0 on success, -1 on failure.
     */
    int usmp_handshake(usmp_transport_t *transport, usmp_t *session);

#ifdef __cplusplus
}
#endif