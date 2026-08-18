#include "usmp_session.h"

#include <stdio.h>
#include <string.h>

#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"
#include "usmp.h"
#include "usmp_crypto.h"
#include "usmp_frame.h"
#include "usmp_port.h"

static const char* TAG = "USMP_SESSION";

static int derive_rekey_keys(bool is_initiator, const uint8_t* tx_key, const uint8_t* rx_key,
                             const uint8_t* session_id, const uint8_t* salt, uint8_t* out_tx_key,
                             uint8_t* out_rx_key) {
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!md) return -1;

  uint8_t secret[USMP_SESSION_KEY_LEN * 2];
  if (is_initiator) {
    memcpy(secret, tx_key, USMP_SESSION_KEY_LEN);
    memcpy(secret + USMP_SESSION_KEY_LEN, rx_key, USMP_SESSION_KEY_LEN);
  } else {
    memcpy(secret, rx_key, USMP_SESSION_KEY_LEN);
    memcpy(secret + USMP_SESSION_KEY_LEN, tx_key, USMP_SESSION_KEY_LEN);
  }

  uint8_t info[10 + USMP_SESSION_ID_LEN];
  memcpy(info, "usmp-rekey", 10);
  memcpy(info + 10, session_id, USMP_SESSION_ID_LEN);

  uint8_t key_material[USMP_SESSION_KEY_LEN * 2];
  int ret = mbedtls_hkdf(md, salt, 32, secret, sizeof(secret), info, sizeof(info), key_material,
                         sizeof(key_material));
  if (ret == 0) {
    if (is_initiator) {
      memcpy(out_tx_key, key_material, USMP_SESSION_KEY_LEN);
      memcpy(out_rx_key, key_material + USMP_SESSION_KEY_LEN, USMP_SESSION_KEY_LEN);
    } else {
      memcpy(out_rx_key, key_material, USMP_SESSION_KEY_LEN);
      memcpy(out_tx_key, key_material + USMP_SESSION_KEY_LEN, USMP_SESSION_KEY_LEN);
    }
  }
  mbedtls_platform_zeroize(secret, sizeof(secret));
  mbedtls_platform_zeroize(key_material, sizeof(key_material));
  return ret;
}

/*
 * Maximum number of consecutive control frames (PING/PONG) processed in a
 * single usmp_recv() call before giving up. Prevents infinite loops if a
 * misbehaving or malicious peer floods control frames.
 */
#define USMP_MAX_CTRL_FRAMES 8

// Helpers ───────────────────────────────────────────────────────────────────

/* The AES-GCM AAD is exactly the 10-byte frame header (all fields but the CRC). */
static void build_aad(uint16_t magic, uint8_t version, uint8_t type, uint32_t seq, uint16_t length,
                      uint8_t* aad) {
  usmp_serialize_header(magic, version, type, seq, length, aad);
}

/*
 * Deterministic 12-byte AES-GCM nonce: seq(4, little-endian) || session_id[0..7].
 * Unique per (key, seq) within a session, so the (key, nonce) pair is never reused.
 */
static void build_nonce(uint32_t seq, const uint8_t* session_id,
                        uint8_t nonce[USMP_GCM_NONCE_LEN]) {
  nonce[0] = (uint8_t)(seq & 0xFF);
  nonce[1] = (uint8_t)((seq >> 8) & 0xFF);
  nonce[2] = (uint8_t)((seq >> 16) & 0xFF);
  nonce[3] = (uint8_t)((seq >> 24) & 0xFF);
  memcpy(nonce + 4, session_id, 8);
}

/*
 * Encrypt one plaintext chunk into a single frame and transmit it, advancing
 * tx_seq. Shared by usmp_send() (DATA / DATA_FRAG) and send_control() (PING,
 * PONG, BYE — empty plaintext). Pass data=NULL, len=0 for a control frame:
 * the AES-GCM output is then just nonce(12) || tag(16) = 28 bytes.
 *
 * Returns: 0 on success, -2 if encryption failed, -1 if the transport send
 * failed. The two error codes let callers log a precise diagnostic.
 */
static int emit_frame(usmp_t* ctx, uint8_t type, const uint8_t* data, uint16_t len) {
  /*
   * TX sequence-exhaustion guard for EVERY frame type. usmp_send() pre-checks
   * the whole (possibly multi-fragment) message, but send_control() reaches this
   * function directly, so this is the single choke point that keeps tx_seq from
   * wrapping back to 0 and reusing a nonce from the start of the session under
   * the unchanged key. 0xFFFFFFFF is the reserved terminal sentinel the RX side
   * rejects, so the last usable sequence number is 0xFFFFFFFE.
   */
  if (ctx->tx_seq >= 0xFFFFFFFF) {
    USMP_LOGE(TAG, "TX sequence overflowed");
    ctx->established = false;
    return -1;
  }

  usmp_packet_t pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.magic = USMP_MAGIC;
  pkt.version = USMP_VERSION;
  pkt.type = type;
  pkt.seq = ctx->tx_seq;

  uint16_t enc_length = USMP_GCM_NONCE_LEN + len + USMP_GCM_TAG_LEN;
  uint8_t aad[10];
  build_aad(pkt.magic, pkt.version, pkt.type, pkt.seq, enc_length, aad);

  uint8_t nonce[USMP_GCM_NONCE_LEN];
  build_nonce(pkt.seq, ctx->session_id, nonce);

  size_t out_len = 0;
  if (usmp_crypto_encrypt(ctx->cipher_suite, ctx->tx_key, nonce, aad, sizeof(aad), data, len,
                          pkt.payload, &out_len) != 0)
    return -2;

  pkt.length = (uint16_t)out_len;

  uint8_t tx_buf[USMP_HEADER_SIZE + USMP_MAX_PAYLOAD];
  uint16_t tx_len = 0;
  usmp_build_packet(&pkt, tx_buf, &tx_len);

  /*
   * Reserve the sequence number BEFORE handing the frame to the transport.
   * The nonce is seq||session_id[0..7] under a fixed key, so a seq must never be
   * reused. Transmit-then-fail is a real failure mode — the UDP ARQ retransmits
   * the ciphertext up to 5 times before it returns -1 — which means the frame is
   * already on the wire when send() reports failure. Advancing tx_seq only on
   * success would let the next send reuse this seq with different plaintext:
   * identical (key, nonce), which breaks AES-GCM confidentiality and leaks the
   * GHASH authentication key. Burn the seq up front, then drop the session on
   * failure so a re-handshake installs a fresh key/session_id before any resend.
   */
  ctx->tx_seq++;
  ctx->last_tx_ms = usmp_port_millis();

  if (ctx->transport.send(&ctx->transport, tx_buf, tx_len) < 0) {
    ctx->established = false;
    return -1;
  }
  return 0;
}

// Send an encrypted control frame (PING, PONG, BYE) with empty plaintext ───
static int send_control(usmp_t* ctx, uint8_t type) { return emit_frame(ctx, type, NULL, 0); }

/*
 * Best-effort graceful BYE, used by usmp_close(). Lets the peer release the
 * session immediately instead of waiting for its inactivity watchdog. The
 * caller owns teardown regardless, so the return value is advisory only.
 */
usmp_err_t usmp_send_bye(usmp_t* ctx) {
  if (!ctx || !ctx->established || !ctx->transport.send) return USMP_ERR_NOT_CONNECTED;
  return (send_control(ctx, USMP_TYPE_BYE) == 0) ? USMP_OK : USMP_ERR_TRANSPORT_FAILED;
}

// Public API ────────────────────────────────────────────────────────────────

usmp_err_t usmp_send(usmp_t* ctx, const uint8_t* data, uint16_t len) {
  if (!ctx || !ctx->established) return USMP_ERR_NOT_CONNECTED;

  uint32_t num_fragments =
      (len == 0) ? 1 : (uint32_t)((len + USMP_MAX_DATA_LEN - 1) / USMP_MAX_DATA_LEN);
  if (num_fragments > (0xFFFFFFFF - ctx->tx_seq)) {
    USMP_LOGE(TAG, "TX sequence overflowed");
    ctx->established = false;
    return USMP_ERR_SEQ_EXHAUSTED;
  }

  if (len > USMP_MAX_DATA_LEN * USMP_MAX_FRAMES) {
    USMP_LOGE(TAG, "Payload too large for fragmentation limits");
    return USMP_ERR_BUFFER_OVERFLOW;
  }

  uint16_t offset = 0;
  char _msg[64];

  while (offset < len || len == 0) {
    uint16_t chunk_len = len - offset;
    if (chunk_len > USMP_MAX_DATA_LEN) {
      chunk_len = USMP_MAX_DATA_LEN;
    }

    uint8_t type = (offset + chunk_len < len) ? USMP_TYPE_DATA_FRAG : USMP_TYPE_DATA;
    uint32_t frame_seq = ctx->tx_seq;  // captured before emit_frame advances it

    int rc = emit_frame(ctx, type, data + offset, chunk_len);
    if (rc != 0) {
      USMP_LOGE(TAG, rc == -2 ? "Encryption failed" : "Send failed");
      return (rc == -2) ? USMP_ERR_CRYPTO_FAILED : USMP_ERR_TRANSPORT_FAILED;
    }

    snprintf(_msg, sizeof(_msg), "TX seq=%lu len=%u type=0x%02x", (unsigned long)frame_seq,
             chunk_len, type);
    USMP_LOGI(TAG, _msg);

    offset += chunk_len;

    if (len == 0) {
      break;
    }
  }

  return 0;
}

int usmp_recv(usmp_t* ctx, uint8_t* out, uint16_t max_len) {
  if (!ctx || !ctx->established) return -1;

  char _msg[64];
  uint8_t rx_buf[USMP_HEADER_SIZE + USMP_MAX_PAYLOAD];
  usmp_packet_t pkt;
  uint8_t dummy_out[32];  // large enough for nonce+tag of empty plaintext

  uint16_t bytes_written = 0;
  uint8_t frame_count = 0;
  uint32_t expected_frag_seq = 0;
  int attempts = 0;

  for (int ctrl_count = 0;;) {
    if (attempts++ >= 10) {
      USMP_LOGW(TAG, "Max receive attempts reached per call — possible flood");
      if (frame_count > 0) {
        /*
         * We already consumed part of a fragmented message and advanced the
         * rx state. Returning 0 here would look like "no data" while silently
         * dropping the partial payload, and the next call would try to
         * reassemble from the middle of the message — corrupt data. Fail hard
         * so the caller reconnects instead.
         */
        USMP_LOGE(TAG, "Incomplete reassembly after max attempts — session desynced");
        ctx->established = false;
        return -1;
      }
      return 0;
    }
    int len = ctx->transport.recv(&ctx->transport, rx_buf, sizeof(rx_buf));
    if (len < 0) {
      USMP_LOGE(TAG, "Recv failed");
      ctx->established = false;
      return -1;
    }

    if (usmp_parse_packet(rx_buf, len, &pkt) != 0) {
      USMP_LOGE(TAG, "Parse failed");
      if (ctx->transport.confirm_authenticated) {
        continue;  // UDP: drop unauthenticated packet and continue reading
      }
      return -1;
    }

    // Sliding replay window check for UDP (L2)
    if (ctx->transport.confirm_authenticated) {
      if (pkt.seq >= 0xFFFFFFFF) {
        USMP_LOGE(TAG, "RX sequence overflowed");
        ctx->established = false;
        return -1;
      }
      if (ctx->rx_seq >= 64 && pkt.seq <= ctx->rx_seq - 64) {
        USMP_LOGD(TAG, "Packet sequence is too old");
        continue;  // drop silently
      }
      if (pkt.seq <= ctx->rx_seq) {
        uint32_t offset = ctx->rx_seq - pkt.seq;
        if ((ctx->rx_window_bitmap & ((uint64_t)1 << offset)) != 0) {
          USMP_LOGD(TAG, "Duplicate packet detected");
          continue;  // duplicate, drop silently
        }
      }
    }

    /* Choose output buffer and max length for decryption */
    uint8_t* dec_dest = NULL;
    uint16_t dec_max = 0;

    uint8_t rekey_salt_buf[32] = {0};

    if (pkt.type == USMP_TYPE_PONG || pkt.type == USMP_TYPE_PING || pkt.type == USMP_TYPE_BYE) {
      if (bytes_written > 0) {
        USMP_LOGE(TAG, "Protocol error: control frame during fragmentation");
        if (ctx->transport.confirm_authenticated)
          continue;  // UDP: drop spoofed frame, keep reading
        return -1;
      }
      dec_dest = dummy_out;
      dec_max = sizeof(dummy_out);
    } else if (pkt.type == USMP_TYPE_REKEY) {
      if (bytes_written > 0) {
        USMP_LOGE(TAG, "Protocol error: REKEY frame during fragmentation");
        if (ctx->transport.confirm_authenticated) continue;
        return -1;
      }
      dec_dest = rekey_salt_buf;
      dec_max = sizeof(rekey_salt_buf);
    } else if (pkt.type == USMP_TYPE_DATA || pkt.type == USMP_TYPE_DATA_FRAG) {
      dec_dest = out + bytes_written;
      if (max_len < bytes_written) {
        USMP_LOGE(TAG, "Buffer overflow sanity check failed");
        return -1;
      }
      dec_max = max_len - bytes_written;
    } else {
      snprintf(_msg, sizeof(_msg), "Unexpected type 0x%02x", pkt.type);
      USMP_LOGE(TAG, _msg);
      if (ctx->transport.confirm_authenticated) continue;  // UDP: drop spoofed frame, keep reading
      return -1;
    }

    /* Verify payload length is valid for AES-GCM (contains at least nonce + tag) */
    if (pkt.length < USMP_GCM_NONCE_LEN + USMP_GCM_TAG_LEN) {
      USMP_LOGE(TAG, "Payload too short");
      if (ctx->transport.confirm_authenticated) continue;  // UDP: drop spoofed frame, keep reading
      return -1;
    }
    uint16_t plain_len = pkt.length - USMP_GCM_NONCE_LEN - USMP_GCM_TAG_LEN;
    if (plain_len > dec_max) {
      USMP_LOGE(TAG, "Buffer too small for payload");
      if (ctx->transport.confirm_authenticated) continue;  // UDP: drop spoofed frame, keep reading
      return -1;
    }

    uint8_t aad[10];
    build_aad(pkt.magic, pkt.version, pkt.type, pkt.seq, pkt.length, aad);

    uint8_t expected_nonce[USMP_GCM_NONCE_LEN];
    expected_nonce[0] = (uint8_t)(pkt.seq & 0xFF);
    expected_nonce[1] = (uint8_t)((pkt.seq >> 8) & 0xFF);
    expected_nonce[2] = (uint8_t)((pkt.seq >> 16) & 0xFF);
    expected_nonce[3] = (uint8_t)((pkt.seq >> 24) & 0xFF);
    memcpy(expected_nonce + 4, ctx->session_id, 8);

    size_t out_len = 0;
    if (usmp_crypto_decrypt(ctx->cipher_suite, ctx->rx_key, expected_nonce, aad, sizeof(aad),
                            pkt.payload, pkt.length, dec_dest, &out_len) != 0) {
      USMP_LOGE(TAG, "Decryption failed");
      if (ctx->transport.confirm_authenticated) {
        continue;  // UDP: drop unauthenticated packet and continue reading
      }
      return -1;
    }

    if (ctx->transport.confirm_authenticated) {
      // Update sliding replay window on successful verification (L2)
      if (pkt.seq > ctx->rx_seq) {
        uint32_t shift = pkt.seq - ctx->rx_seq;
        if (shift < 64) {
          ctx->rx_window_bitmap = (ctx->rx_window_bitmap << shift) | 1;
        } else {
          ctx->rx_window_bitmap = 1;
        }
        ctx->rx_seq = pkt.seq;
      } else {
        uint32_t offset = ctx->rx_seq - pkt.seq;
        ctx->rx_window_bitmap |= ((uint64_t)1 << offset);
      }
      ctx->transport.confirm_authenticated(&ctx->transport, pkt.seq);
    } else {
      if (ctx->rx_seq >= 0xFFFFFFFF) {
        USMP_LOGE(TAG, "RX sequence overflowed");
        ctx->established = false;
        return -1;
      }

      if (pkt.seq != ctx->rx_seq) {
        snprintf(_msg, sizeof(_msg), "Seq mismatch: expected %lu got %lu",
                 (unsigned long)ctx->rx_seq, (unsigned long)pkt.seq);
        USMP_LOGE(TAG, _msg);
        return -1;
      }
      ctx->rx_seq++;
    }

    /* Handle control frames and loop back for the next frame */
    if (pkt.type == USMP_TYPE_PONG) {
      USMP_LOGI(TAG, "PONG received");
      if (++ctrl_count > USMP_MAX_CTRL_FRAMES) {
        USMP_LOGE(TAG, "Too many consecutive control frames — possible flood");
        return -1;
      }
      if (ctx->transport.available && ctx->transport.available(&ctx->transport) <= 0) {
        return 0;
      }
      continue;

    } else if (pkt.type == USMP_TYPE_PING) {
      USMP_LOGI(TAG, "PING received — responding with PONG");
      if (send_control(ctx, USMP_TYPE_PONG) != 0) {
        USMP_LOGE(TAG, "Failed to send PONG");
        ctx->established = false;
        return -1;
      }
      if (++ctrl_count > USMP_MAX_CTRL_FRAMES) {
        USMP_LOGE(TAG, "Too many consecutive control frames — possible flood");
        return -1;
      }
      if (ctx->transport.available && ctx->transport.available(&ctx->transport) <= 0) {
        return 0;
      }
      continue;

    } else if (pkt.type == USMP_TYPE_BYE) {
      USMP_LOGI(TAG, "BYE received — session closed by peer");
      ctx->established = false;
      return -1;
    } else if (pkt.type == USMP_TYPE_REKEY) {
      USMP_LOGI(TAG, "REKEY frame received — rotating session keys");
      if (out_len != 32) {
        USMP_LOGE(TAG, "Invalid REKEY payload size");
        return -1;
      }
      uint8_t new_tx[USMP_SESSION_KEY_LEN];
      uint8_t new_rx[USMP_SESSION_KEY_LEN];
      if (derive_rekey_keys(false, ctx->tx_key, ctx->rx_key, ctx->session_id, rekey_salt_buf,
                            new_tx, new_rx) != 0) {
        USMP_LOGE(TAG, "Failed to derive new keys on REKEY");
        return -1;
      }
      memcpy(ctx->tx_key, new_tx, USMP_SESSION_KEY_LEN);
      memcpy(ctx->rx_key, new_rx, USMP_SESSION_KEY_LEN);
      ctx->tx_seq = 0;
      ctx->rx_seq = 0;
      ctx->rx_window_bitmap = 0;

      if (ctx->transport.set_session_keys) {
        ctx->transport.set_session_keys(&ctx->transport, ctx->tx_key, ctx->rx_key);
      }

      mbedtls_platform_zeroize(new_tx, sizeof(new_tx));
      mbedtls_platform_zeroize(new_rx, sizeof(new_rx));
      mbedtls_platform_zeroize(rekey_salt_buf, sizeof(rekey_salt_buf));

      if (++ctrl_count > USMP_MAX_CTRL_FRAMES) {
        USMP_LOGE(TAG, "Too many consecutive control frames — possible flood");
        return -1;
      }
      if (ctx->transport.available && ctx->transport.available(&ctx->transport) <= 0) {
        return 0;
      }
      continue;
    }

    /* It's a DATA or DATA_FRAG frame */
    if (frame_count > 0) {
      if (pkt.seq != expected_frag_seq) {
        USMP_LOGE(TAG, "Protocol error: out-of-order fragment sequence");
        return -1;
      }
      expected_frag_seq++;
    } else {
      expected_frag_seq = pkt.seq + 1;
    }

    bytes_written += (uint16_t)out_len;
    frame_count++;

    if (pkt.type == USMP_TYPE_DATA) {
      /* Reassembly complete */
      snprintf(_msg, sizeof(_msg), "RX total len=%u frames=%u", bytes_written, frame_count);
      USMP_LOGI(TAG, _msg);
      return bytes_written;
    }

    /* It was a DATA_FRAG frame. Verify we haven't hit the frame limit */
    if (frame_count >= USMP_MAX_FRAMES) {
      USMP_LOGE(TAG, "Protocol error: exceeded max fragments limit");
      return -1;
    }

    /* Loop back to read the next fragment immediately */
  }
}

usmp_err_t usmp_ping(usmp_t* ctx) {
  if (!ctx || !ctx->established) return USMP_ERR_NOT_CONNECTED;

  if (send_control(ctx, USMP_TYPE_PING) != 0) {
    USMP_LOGE(TAG, "PING send failed");
    ctx->established = false;
    return USMP_ERR_TRANSPORT_FAILED;
  }

  USMP_LOGI(TAG, "PING sent");
  return USMP_OK;
}

usmp_err_t usmp_keepalive_tick(usmp_t* ctx) {
  if (!ctx || !ctx->established) return USMP_ERR_NOT_CONNECTED;

  if (ctx->keepalive_ms == 0) return USMP_OK;  // disabled

  uint32_t now = usmp_port_millis();
  if ((now - ctx->last_tx_ms) >= ctx->keepalive_ms) return usmp_ping(ctx);

  return USMP_OK;
}

usmp_err_t usmp_rekey(usmp_t* ctx) {
  if (!ctx || !ctx->established) return USMP_ERR_NOT_CONNECTED;

  uint8_t salt[32];
  if (usmp_port_random(salt, sizeof(salt)) != 0) {
    USMP_LOGE(TAG, "Failed to generate random salt for rekey");
    return USMP_ERR_CRYPTO_FAILED;
  }

  int rc = emit_frame(ctx, USMP_TYPE_REKEY, salt, sizeof(salt));
  if (rc != 0) {
    USMP_LOGE(TAG, "Failed to emit REKEY frame");
    mbedtls_platform_zeroize(salt, sizeof(salt));
    return USMP_ERR_TRANSPORT_FAILED;
  }

  uint8_t new_tx[USMP_SESSION_KEY_LEN];
  uint8_t new_rx[USMP_SESSION_KEY_LEN];
  if (derive_rekey_keys(true, ctx->tx_key, ctx->rx_key, ctx->session_id, salt, new_tx, new_rx) !=
      0) {
    USMP_LOGE(TAG, "Failed to derive new keys for rekey");
    mbedtls_platform_zeroize(salt, sizeof(salt));
    return USMP_ERR_CRYPTO_FAILED;
  }

  memcpy(ctx->tx_key, new_tx, USMP_SESSION_KEY_LEN);
  memcpy(ctx->rx_key, new_rx, USMP_SESSION_KEY_LEN);
  ctx->tx_seq = 0;
  ctx->rx_seq = 0;
  ctx->rx_window_bitmap = 0;

  if (ctx->transport.set_session_keys) {
    ctx->transport.set_session_keys(&ctx->transport, ctx->tx_key, ctx->rx_key);
  }

  mbedtls_platform_zeroize(salt, sizeof(salt));
  mbedtls_platform_zeroize(new_tx, sizeof(new_tx));
  mbedtls_platform_zeroize(new_rx, sizeof(new_rx));

  USMP_LOGI(TAG, "In-band session rekeying initiated successfully");
  return USMP_OK;
}
