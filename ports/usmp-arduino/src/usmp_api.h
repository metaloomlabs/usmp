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
#define USMP_VERSION_MINOR 4
#define USMP_VERSION_PATCH 7

/**
 * Get the library version string at runtime (e.g. "0.4.7").
 */
const char* usmp_get_version(void);

// Configuration ─────────────────────────────────────────────────────────────

/*
 * USMP_PSK compile-time default has been REMOVED for security reasons.
 *
 * Set the PSK at runtime via the USMPClient constructor:
 *
 *   USMPClient usmp("my-provisioned-psk");
 *
 * Do NOT hardcode PSKs in your sketch source code — they will be
 * visible in compiled firmware and can be extracted by an attacker.
 * Load your PSK from secure storage, NVS, EEPROM, or a secure element.
 */
#ifdef USMP_PSK
#  error "USMP_PSK compile-time PSK is no longer supported. " \
         "Pass the PSK to USMPClient() at runtime instead."
#endif

#ifndef USMP_DEFAULT_PORT
#define USMP_DEFAULT_PORT 9000
#endif

/*
 * USMP_CONNECT_RETRIES / USMP_CONNECT_RETRY_MS
 * Not yet used internally. Available for caller retry loops.
 */
#ifndef USMP_CONNECT_RETRIES
#define USMP_CONNECT_RETRIES 10  // Not yet implemented internally
#endif

#ifndef USMP_CONNECT_RETRY_MS
#define USMP_CONNECT_RETRY_MS 2000  // Not yet implemented internally
#endif

// Constants ─────────────────────────────────────────────────────────────────
#define USMP_DEVICE_ID_LEN   6
#define USMP_SESSION_ID_LEN  16   // Upgraded from 4 → 16 bytes (128-bit)
#define USMP_SESSION_KEY_LEN 32

/*
 * USMP_MAX_DATA_LEN: maximum application payload per send() call (452 bytes).
 * Frame payload budget (480 bytes) minus AES-GCM nonce (12) and tag (16).
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
  const uint8_t *psk;
  size_t psk_len;
} usmp_t;

// Connection API ────────────────────────────────────────────────────────────

/**
 * Connect using a transport and perform USMP handshake.
 * ctx->psk and ctx->psk_len must be set before calling.
 */
int usmp_connect(usmp_t *ctx, usmp_transport_t *transport);

/**
 * Reconnect — re-dials transport and performs full handshake.
 * Resets tx_seq and rx_seq.
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
 */
int usmp_send(usmp_t *ctx, const uint8_t *data, uint16_t len);

/**
 * Receive and decrypt data. Handles inbound PING/PONG transparently.
 * Returns byte count on success, -1 on failure.
 */
int usmp_recv(usmp_t *ctx, uint8_t *out, uint16_t max_len);

// Keepalive API ─────────────────────────────────────────────────────────────

/** Send an encrypted PING frame. */
int usmp_ping(usmp_t *ctx);

/** Send PING if keepalive_ms has elapsed since last tx. No-op if keepalive_ms==0. */
int usmp_keepalive_tick(usmp_t *ctx);

#ifdef __cplusplus
}
#endif