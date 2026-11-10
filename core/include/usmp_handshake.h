#pragma once

#include "usmp.h"
#include "usmp_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

// Handshake constants
#define USMP_NONCE_LEN 32  // bytes — server challenge nonce
#define USMP_HMAC_LEN 32   // bytes — HMAC-SHA256 output

/**
 * Perform USMP mutual-auth handshake over the given transport (synchronous).
 * Populates session->device_id, session_id, tx_key, rx_key, established.
 * Returns USMP_OK (0) on success, or a negative usmp_err_t code on failure.
 */
usmp_err_t usmp_handshake(usmp_transport_t* transport, usmp_t* session);

/**
 * Start non-blocking handshake state machine.
 * Generates client Curve25519 keypair, emits HELLO packet, and transitions
 * session->state to USMP_STATE_AWAITING_CHALLENGE.
 * Returns USMP_OK on success, or negative usmp_err_t on failure.
 */
usmp_err_t usmp_handshake_start(usmp_transport_t* transport, usmp_t* session);

/**
 * Advance non-blocking handshake state machine by one tick.
 * In USMP_STATE_AWAITING_CHALLENGE: reads CHALLENGE, derives keys, sends HELLO_ACK.
 * In USMP_STATE_AWAITING_SESSION_OK: reads SESSION_OK, verifies server HMAC, completes session.
 * Returns USMP_OK while waiting/in-progress or completed, or negative usmp_err_t on error.
 */
usmp_err_t usmp_handshake_step(usmp_transport_t* transport, usmp_t* session);

/**
 * Abort active handshake and clean up internal handshake contexts and buffers.
 */
void usmp_handshake_abort(usmp_transport_t* transport, usmp_t* session);

#ifdef __cplusplus
}
#endif