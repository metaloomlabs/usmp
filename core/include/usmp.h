#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "usmp_frame.h"
#include "usmp_port.h"
#include "usmp_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

// Version ───────────────────────────────────────────────────────────────────
#define USMP_VERSION_MAJOR 1
#define USMP_VERSION_MINOR 2
#define USMP_VERSION_PATCH 0

/**
 * Get the library version string at runtime (e.g. "1.1.0").
 */
const char* usmp_get_version(void);

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
#error \
    "USMP_PSK compile-time PSK is no longer supported. " \
         "Set ctx.psk and ctx.psk_len at runtime instead. " \
         "See core/include/usmp.h for details."
#endif

#ifndef USMP_DEFAULT_PORT
#define USMP_DEFAULT_PORT 9000
#endif

/*
 * USMP_CONNECT_RETRIES / USMP_CONNECT_RETRY_MS
 * Defined for user convenience — not yet used internally by the library.
 * Callers can use these in their own retry loops (see examples/project_tcp/esp32/main/app.c).
 */
#ifndef USMP_CONNECT_RETRIES
#define USMP_CONNECT_RETRIES 10  // Unused internally (caller convenience only)
#endif

#ifndef USMP_CONNECT_RETRY_MS
#define USMP_CONNECT_RETRY_MS 2000  // Unused internally (caller convenience only)
#endif

// Constants ─────────────────────────────────────────────────────────────────
#define USMP_DEVICE_ID_LEN 6
#define USMP_SESSION_ID_LEN 16  // Upgraded from 4 → 16 bytes (128-bit)
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

typedef enum {
  USMP_OK                      =  0,
  USMP_ERR_INVALID_ARG         = -1,
  USMP_ERR_TRANSPORT_FAILED    = -2,
  USMP_ERR_AUTH_FAILED         = -3,
  USMP_ERR_TIMEOUT             = -4,
  USMP_ERR_REPLAY_DETECTED     = -5,
  USMP_ERR_BUFFER_OVERFLOW     = -6,
  USMP_ERR_SEQ_EXHAUSTED       = -7,
  USMP_ERR_CRYPTO_FAILED       = -8,
  USMP_ERR_NOT_CONNECTED       = -9,
} usmp_err_t;

typedef enum {
  USMP_CIPHER_AES256_GCM = 1,
  USMP_CIPHER_CHACHA20_POLY1305 = 2,
} usmp_cipher_suite_t;

// Session context ───────────────────────────────────────────────────────────
typedef struct {
  uint8_t device_id[USMP_DEVICE_ID_LEN];
  uint8_t session_id[USMP_SESSION_ID_LEN];
  uint8_t tx_key[USMP_SESSION_KEY_LEN];
  uint8_t rx_key[USMP_SESSION_KEY_LEN];
  bool established;
  usmp_transport_t transport;
  uint32_t tx_seq;
  uint32_t rx_seq;
  uint32_t keepalive_ms;
  uint32_t last_tx_ms;
  uint64_t rx_window_bitmap;
  uint8_t cipher_suite;

  /*
   * Runtime PSK — must be set before calling usmp_connect().
   * Points to caller-managed memory; must remain valid for the
   * lifetime of the session.
   */
  const uint8_t* psk;
  size_t psk_len;
} usmp_t;

// Threading ─────────────────────────────────────────────────────────────────
//
// A usmp_t session is NOT thread-safe and carries no internal locking. All
// calls that touch one session (usmp_send, usmp_recv, usmp_ping,
// usmp_keepalive_tick, usmp_close, usmp_reconnect) must be serialized by the
// caller. Concurrent senders in particular would race on tx_seq and reuse an
// AES-GCM nonce. On FreeRTOS/ESP32, drive a session from a single task or guard
// it with your own mutex. Distinct sessions on distinct usmp_t objects are
// independent and may run on separate threads.

// Connection API ────────────────────────────────────────────────────────────

/**
 * Connect using a transport and perform USMP handshake.
 * ctx->psk and ctx->psk_len must be set before calling.
 * Returns USMP_OK (0) on success, or a negative usmp_err_t code on failure.
 */
usmp_err_t usmp_connect(usmp_t* ctx, usmp_transport_t* transport);

/**
 * Explicit reconnect — re-dials transport and performs a full new handshake.
 * Resets tx_seq and rx_seq. Caller must handle session change.
 * Returns USMP_OK (0) on success, or a negative usmp_err_t code on failure.
 */
usmp_err_t usmp_reconnect(usmp_t* ctx);

/**
 * Close the USMP session gracefully.
 */
void usmp_close(usmp_t* ctx);

/**
 * Check if session is established.
 */
static inline bool usmp_is_connected(const usmp_t* ctx) { return ctx && ctx->established; }

// Data API ──────────────────────────────────────────────────────────────────

/**
 * Send encrypted data. Max len: USMP_MAX_DATA_LEN (452) bytes.
 * Returns USMP_OK (0) on success, or a negative usmp_err_t code on failure.
 */
usmp_err_t usmp_send(usmp_t* ctx, const uint8_t* data, uint16_t len);

/**
 * Receive and decrypt data. Transparently handles inbound PING/PONG frames
 * (up to 8 consecutive control frames before returning error).
 * Returns byte count (>0) on success, 0 on timeout/empty, or negative usmp_err_t code on error.
 */
int usmp_recv(usmp_t* ctx, uint8_t* out, uint16_t max_len);

// Keepalive API ─────────────────────────────────────────────────────────────

/**
 * Send an encrypted PING frame. Updates last_tx_ms.
 * Returns USMP_OK (0) on success, or a negative usmp_err_t code on failure (dead socket).
 */
usmp_err_t usmp_ping(usmp_t* ctx);

/**
 * Call in main loop. Sends PING if keepalive_ms has elapsed since last tx.
 * No-op if ctx->keepalive_ms == 0.
 * Returns USMP_OK (0) ok, or a negative usmp_err_t code if PING failed (time to call usmp_reconnect).
 */
usmp_err_t usmp_keepalive_tick(usmp_t* ctx);

/**
 * Perform in-band session rekeying. Rotates tx_key and rx_key using HKDF
 * and resets tx_seq and rx_seq to 0 without tearing down the socket.
 * Returns USMP_OK (0) on success, or a negative usmp_err_t code on failure.
 */
usmp_err_t usmp_rekey(usmp_t* ctx);

#ifdef __cplusplus
}
#endif