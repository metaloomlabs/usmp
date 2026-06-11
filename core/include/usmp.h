#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "usmp_frame.h"
#include "usmp_transport.h"

#ifdef __cplusplus
extern "C"
{
#endif

// ── Version ───────────────────────────────────────────────────────────────────
#define USMP_VERSION_MAJOR 0
#define USMP_VERSION_MINOR 2
#define USMP_VERSION_PATCH 6

// ── Configuration ─────────────────────────────────────────────────────────────
#ifndef USMP_PSK
#define USMP_PSK "usmp-dev-psk-change-me-before-prod"
#endif

#ifndef USMP_DEFAULT_PORT
#define USMP_DEFAULT_PORT 9000
#endif

#ifndef USMP_CONNECT_RETRIES
#define USMP_CONNECT_RETRIES 10
#endif

#ifndef USMP_CONNECT_RETRY_MS
#define USMP_CONNECT_RETRY_MS 2000
#endif

// ── Constants ─────────────────────────────────────────────────────────────────
#define USMP_DEVICE_ID_LEN 6
#define USMP_SESSION_ID_LEN 4
#define USMP_SESSION_KEY_LEN 32
#define USMP_MAX_DATA_LEN (USMP_MAX_PAYLOAD - USMP_GCM_TAG_LEN)

    // ── Session context ───────────────────────────────────────────────────────────
    typedef struct
    {
        uint8_t device_id[USMP_DEVICE_ID_LEN];
        uint8_t session_id[USMP_SESSION_ID_LEN];
        uint8_t session_key[USMP_SESSION_KEY_LEN];
        bool established;
        usmp_transport_t transport;
        uint32_t tx_seq;
        uint32_t rx_seq;
        uint32_t keepalive_ms;
        uint32_t last_tx_ms;
        // ── Runtime PSK — overrides USMP_PSK macro when set ────────────────────
        const uint8_t *psk; // NULL = use USMP_PSK compile-time default
        size_t psk_len;
    } usmp_t;

    // ── Connection API ────────────────────────────────────────────────────────────

    /**
     * Connect using a transport and perform USMP handshake.
     */
    int usmp_connect(usmp_t *ctx, usmp_transport_t *transport);

    /**
     * Explicit reconnect — re-dials transport and performs a full new handshake.
     * Resets tx_seq and rx_seq. Caller must handle session change.
     * Returns 0 on success, -1 on failure.
     */
    int usmp_reconnect(usmp_t *ctx);

    /**
     * Close the USMP session gracefully.
     */
    void usmp_close(usmp_t *ctx);

    /**
     * Check if session is established.
     */
    static inline bool usmp_is_connected(const usmp_t *ctx)
    {
        return ctx && ctx->established;
    }

    // ── Data API ──────────────────────────────────────────────────────────────────

    /**
     * Send encrypted data. Max len: USMP_MAX_DATA_LEN bytes.
     * Returns 0 on success, -1 on failure.
     */
    int usmp_send(usmp_t *ctx, const uint8_t *data, uint16_t len);

    /**
     * Receive and decrypt data. Transparently handles inbound PONG frames.
     * Returns byte count on success, -1 on failure.
     */
    int usmp_recv(usmp_t *ctx, uint8_t *out, uint16_t max_len);

    // ── Keepalive API ─────────────────────────────────────────────────────────────

    /**
     * Send an encrypted PING frame. Updates last_tx_ms.
     * Returns 0 on success, -1 on failure (dead socket).
     */
    int usmp_ping(usmp_t *ctx);

    /**
     * Call in main loop. Sends PING if keepalive_ms has elapsed since last tx.
     * No-op if ctx->keepalive_ms == 0.
     * Returns 0 ok, -1 if PING failed (time to call usmp_reconnect).
     */
    int usmp_keepalive_tick(usmp_t *ctx);

#ifdef __cplusplus
}
#endif