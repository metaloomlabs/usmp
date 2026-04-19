#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Version ───────────────────────────────────────────────────────────────────
#define DXP_VERSION_MAJOR 0
#define DXP_VERSION_MINOR 1
#define DXP_VERSION_PATCH 0

// ── Configuration ─────────────────────────────────────────────────────────────

// Pre-shared key — change before production
#ifndef DXP_PSK
#define DXP_PSK "dxp-dev-psk-change-me-before-prod"
#endif

// Default server connection settings
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
#define DXP_DEVICE_ID_LEN   6
#define DXP_SESSION_ID_LEN  4
#define DXP_SESSION_KEY_LEN 32
#define DXP_MAX_DATA_LEN    448  // max plaintext per frame

// ── Session context ───────────────────────────────────────────────────────────

typedef struct {
    uint8_t  device_id[DXP_DEVICE_ID_LEN];
    uint8_t  session_id[DXP_SESSION_ID_LEN];
    uint8_t  session_key[DXP_SESSION_KEY_LEN];
    bool     established;
    int      sock;
    uint32_t tx_seq;
    uint32_t rx_seq;
} dxp_t;

// ── API ───────────────────────────────────────────────────────────────────────

/**
 * Connect to a DXP server and perform the full handshake.
 *
 * @param ctx        DXP context (caller-allocated)
 * @param server_ip  Server IPv4 address string
 * @param port       Server TCP port
 * @return           0 on success, -1 on failure
 */
int dxp_connect(dxp_t *ctx, const char *server_ip, int port);

/**
 * Send data over an established DXP session.
 * Data is encrypted with AES-256-GCM before transmission.
 *
 * @param ctx   DXP context
 * @param data  Plaintext data to send
 * @param len   Length of data (max DXP_MAX_DATA_LEN)
 * @return      0 on success, -1 on failure
 */
int dxp_send(dxp_t *ctx, const uint8_t *data, uint16_t len);

/**
 * Receive data over an established DXP session.
 * Decrypts and authenticates the incoming frame.
 *
 * @param ctx     DXP context
 * @param out     Buffer to write plaintext into
 * @param max_len Size of out buffer
 * @return        Number of bytes received, -1 on failure
 */
int dxp_recv(dxp_t *ctx, uint8_t *out, uint16_t max_len);

/**
 * Close the DXP session and TCP connection.
 *
 * @param ctx DXP context
 */
void dxp_close(dxp_t *ctx);

/**
 * Check if a DXP session is established.
 *
 * @param ctx DXP context
 * @return    true if established
 */
static inline bool dxp_is_connected(const dxp_t *ctx)
{
    return ctx && ctx->established && ctx->sock >= 0;
}

#ifdef __cplusplus
}
#endif