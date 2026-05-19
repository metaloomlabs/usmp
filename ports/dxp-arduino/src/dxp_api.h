#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dxp_frame.h"
#include "dxp_transport.h"

#ifdef __cplusplus
extern "C"
{
#endif

// ── Version ───────────────────────────────────────────────────────────────────
#define DXP_VERSION_MAJOR 0
#define DXP_VERSION_MINOR 2
#define DXP_VERSION_PATCH 0

// ── Configuration ─────────────────────────────────────────────────────────────
#ifndef DXP_PSK
#define DXP_PSK "dxp-dev-psk-change-me-before-prod"
#endif

#ifndef DXP_DEFAULT_PORT
#define DXP_DEFAULT_PORT 9000
#endif

#ifndef DXP_CONNECT_RETRIES
#define DXP_CONNECT_RETRIES 10
#endif

#ifndef DXP_CONNECT_RETRY_MS
#define DXP_CONNECT_RETRY_MS 2000
#endif

// ── Constants ─────────────────────────────────────────────────────────────────
#define DXP_DEVICE_ID_LEN 6
#define DXP_SESSION_ID_LEN 4
#define DXP_SESSION_KEY_LEN 32
#define DXP_MAX_DATA_LEN (DXP_MAX_PAYLOAD - DXP_GCM_TAG_LEN)

    // ── Session context ───────────────────────────────────────────────────────────
    typedef struct
    {
        uint8_t device_id[DXP_DEVICE_ID_LEN];
        uint8_t session_id[DXP_SESSION_ID_LEN];
        uint8_t session_key[DXP_SESSION_KEY_LEN];
        bool established;
        dxp_transport_t transport;
        uint32_t tx_seq;
        uint32_t rx_seq;
        uint32_t keepalive_ms;
        uint32_t last_tx_ms;
        // ── Runtime PSK — overrides DXP_PSK macro when set ────────────────────
        const uint8_t *psk; // NULL = use DXP_PSK compile-time default
        size_t psk_len;
    } dxp_t;

    // ── Connection API ────────────────────────────────────────────────────────────

    /**
     * Connect using a transport and perform DXP handshake.
     */
    int dxp_connect(dxp_t *ctx, dxp_transport_t *transport);

    /**
     * Explicit reconnect — re-dials transport and performs a full new handshake.
     * Resets tx_seq and rx_seq. Caller must handle session change.
     * Returns 0 on success, -1 on failure.
     */
    int dxp_reconnect(dxp_t *ctx);

    /**
     * Close the DXP session gracefully.
     */
    void dxp_close(dxp_t *ctx);

    /**
     * Check if session is established.
     */
    static inline bool dxp_is_connected(const dxp_t *ctx)
    {
        return ctx && ctx->established;
    }

    // ── Data API ──────────────────────────────────────────────────────────────────

    /**
     * Send encrypted data. Max len: DXP_MAX_DATA_LEN bytes.
     * Returns 0 on success, -1 on failure.
     */
    int dxp_send(dxp_t *ctx, const uint8_t *data, uint16_t len);

    /**
     * Receive and decrypt data. Transparently handles inbound PONG frames.
     * Returns byte count on success, -1 on failure.
     */
    int dxp_recv(dxp_t *ctx, uint8_t *out, uint16_t max_len);

    // ── Keepalive API ─────────────────────────────────────────────────────────────

    /**
     * Send an encrypted PING frame. Updates last_tx_ms.
     * Returns 0 on success, -1 on failure (dead socket).
     */
    int dxp_ping(dxp_t *ctx);

    /**
     * Call in main loop. Sends PING if keepalive_ms has elapsed since last tx.
     * No-op if ctx->keepalive_ms == 0.
     * Returns 0 ok, -1 if PING failed (time to call dxp_reconnect).
     */
    int dxp_keepalive_tick(dxp_t *ctx);

#ifdef __cplusplus
}
#endif