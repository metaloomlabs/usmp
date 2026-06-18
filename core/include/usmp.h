#pragma once

#include "usmp_frame.h"
#include "usmp_transport.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Version ───────────────────────────────────────────────────────────────────
#define USMP_VERSION_MAJOR 0
#define USMP_VERSION_MINOR 3
#define USMP_VERSION_PATCH 0

// Configuration ─────────────────────────────────────────────────────────────

/*
 * USMP_PSK compile-time default has been REMOVED for security reasons.
 *
 * Compile-time PSKs appear in all compiled binaries and in version control
 * history, enabling any attacker who obtains the firmware to impersonate
 * any device or server.
 *
 * Instead, set the PSK at runtime:
 *
 *   usmp_t ctx = {0};
 *   ctx.psk     = my_provisioned_psk_bytes;   // loaded from secure storage
 *   ctx.psk_len = my_psk_len;
 *   usmp_connect(&ctx, &transport);
 *
 * If you previously relied on a compile-time USMP_PSK define, remove it
 * and provision the PSK via secure storage, an HSM, or secure boot.
 */
#ifdef USMP_PSK
#  error "USMP_PSK compile-time PSK is no longer supported. " \
         "Set ctx.psk and ctx.psk_len at runtime instead. " \
         "See core/include/usmp.h for details."
#endif

#ifndef USMP_DEFAULT_PORT
#define USMP_DEFAULT_PORT 9000
#endif

/*
 * USMP_CONNECT_RETRIES / USMP_CONNECT_RETRY_MS
 * Defined for user convenience — not yet used internally by the library.
 * Callers can use these in their own retry loops (see firmware/main/app.c).
 */
#ifndef USMP_CONNECT_RETRIES
#define USMP_CONNECT_RETRIES 10  // Unused internally (caller convenience only)
#endif

#ifndef USMP_CONNECT_RETRY_MS
#define USMP_CONNECT_RETRY_MS 2000  // Unused internally (caller convenience only)
#endif

// Constants ─────────────────────────────────────────────────────────────────
#define USMP_DEVICE_ID_LEN   6
#define USMP_SESSION_ID_LEN  16   // Upgraded from 4 → 16 bytes (128-bit)
#define USMP_SESSION_KEY_LEN 32

/*
 * USMP_MAX_DATA_LEN: maximum application payload per send() call.
 *
 * Frame payload budget: USMP_MAX_PAYLOAD (480 bytes)
 *   - AES-GCM nonce:    12 bytes (prepended, random per message)
 *   - AES-GCM tag:      16 bytes (appended)
 *   = max plaintext:   452 bytes
 */
#define USMP_MAX_DATA_LEN (USMP_MAX_PAYLOAD - USMP_GCM_TAG_LEN - 12)

// Session context ───────────────────────────────────────────────────────────
typedef struct {
  uint8_t device_id[USMP_DEVICE_ID_LEN];
  uint8_t session_id[USMP_SESSION_ID_LEN];
  uint8_t session_key[USMP_SESSION_KEY_LEN];
  bool established;
  usmp_transport_t transport;
  uint32_t tx_seq;
  uint32_t rx_seq;
  uint32_t keepalive_ms;
  uint32_t last_tx_ms;

  /*
   * Runtime PSK — must be set before calling usmp_connect().
   * Points to caller-managed memory; must remain valid for the
   * lifetime of the session.
   */
  const uint8_t *psk;
  size_t psk_len;
} usmp_t;

// Connection API ────────────────────────────────────────────────────────────

/**
 * Connect using a transport and perform USMP handshake.
 * ctx->psk and ctx->psk_len must be set before calling.
 * Returns 0 on success, -1 on failure.
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
static inline bool usmp_is_connected(const usmp_t *ctx) {
  return ctx && ctx->established;
}

// Data API ──────────────────────────────────────────────────────────────────

/**
 * Send encrypted data. Max len: USMP_MAX_DATA_LEN (452) bytes.
 * Returns 0 on success, -1 on failure.
 */
int usmp_send(usmp_t *ctx, const uint8_t *data, uint16_t len);

/**
 * Receive and decrypt data. Transparently handles inbound PING/PONG frames
 * (up to 8 consecutive control frames before returning error).
 * Returns byte count on success, -1 on failure.
 */
int usmp_recv(usmp_t *ctx, uint8_t *out, uint16_t max_len);

// Keepalive API ─────────────────────────────────────────────────────────────

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