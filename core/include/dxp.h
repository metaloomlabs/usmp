#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

// ── Version ───────────────────────────────────────────────────────────────────
#define DXP_VERSION_MAJOR 0
#define DXP_VERSION_MINOR 1
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
#define DXP_MAX_DATA_LEN 448

    // ── Transport interface ───────────────────────────────────────────────────────
    typedef struct dxp_transport_s
    {
        int (*send)(struct dxp_transport_s *t, const uint8_t *data, size_t len);
        int (*recv)(struct dxp_transport_s *t, uint8_t *buf, size_t max_len);
        void (*close)(struct dxp_transport_s *t);
        void *ctx; // transport-specific state (socket fd, UART handle, etc.)
    } dxp_transport_t;

    // ── Session context ───────────────────────────────────────────────────────────
    typedef struct
    {
        uint8_t device_id[DXP_DEVICE_ID_LEN];
        uint8_t session_id[DXP_SESSION_ID_LEN];
        uint8_t session_key[DXP_SESSION_KEY_LEN];
        bool established;
        dxp_transport_t transport; // ← replaces raw sock
        uint32_t tx_seq;
        uint32_t rx_seq;
    } dxp_t;

    // ── API ───────────────────────────────────────────────────────────────────────

    /**
     * Connect using a pre-configured transport and perform DXP handshake.
     * The transport must already be connected before calling this.
     */
    int dxp_connect(dxp_t *ctx, dxp_transport_t *transport);

    /**
     * Send data over an established DXP session (encrypted).
     */
    int dxp_send(dxp_t *ctx, const uint8_t *data, uint16_t len);

    /**
     * Receive and decrypt data from an established DXP session.
     */
    int dxp_recv(dxp_t *ctx, uint8_t *out, uint16_t max_len);

    /**
     * Close the DXP session.
     */
    void dxp_close(dxp_t *ctx);

    /**
     * Check if session is established.
     */
    static inline bool dxp_is_connected(const dxp_t *ctx)
    {
        return ctx && ctx->established;
    }

#ifdef __cplusplus
}
#endif