#include "usmp_handshake.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mbedtls/constant_time.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/entropy.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"
#include "usmp.h"
#include "usmp_frame.h"
#include "usmp_port.h"

static const char* TAG = "USMP_HS";

#define PUB_KEY_LEN 32
#define USMP_COOKIE_LEN 16  // HELLO_RETRY anti-DoS cookie length

static int derive_session_keys(const uint8_t* shared_secret, size_t secret_len,
                               const uint8_t* nonce, size_t nonce_len, const uint8_t* pub_c,
                               const uint8_t* pub_s, uint8_t* out_c2s, uint8_t* out_s2c) {
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!md) return -1;

  uint8_t info[7 + PUB_KEY_LEN + PUB_KEY_LEN];
  memcpy(info, "usmp-v2", 7);
  memcpy(info + 7, pub_c, PUB_KEY_LEN);
  memcpy(info + 7 + PUB_KEY_LEN, pub_s, PUB_KEY_LEN);

  /* Derive 64 bytes: first 32 = k_c2s, last 32 = k_s2c */
  uint8_t key_material[USMP_SESSION_KEY_LEN * 2];
  int ret = mbedtls_hkdf(md, nonce, nonce_len, shared_secret, secret_len, info, sizeof(info),
                         key_material, sizeof(key_material));
  if (ret == 0) {
    memcpy(out_c2s, key_material, USMP_SESSION_KEY_LEN);
    memcpy(out_s2c, key_material + USMP_SESSION_KEY_LEN, USMP_SESSION_KEY_LEN);
  }
  mbedtls_platform_zeroize(key_material, sizeof(key_material));
  return ret;
}

static int compute_hmac(const uint8_t* psk, size_t psk_len, const uint8_t* data, size_t data_len,
                        uint8_t* out) {
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);

  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) return -1;

  int ret = 0;
  if (mbedtls_md_setup(&ctx, info, 1) != 0) {
    ret = -1;
    goto done;
  }
  if (mbedtls_md_hmac_starts(&ctx, psk, psk_len) != 0) {
    ret = -1;
    goto done;
  }
  if (mbedtls_md_hmac_update(&ctx, data, data_len) != 0) {
    ret = -1;
    goto done;
  }
  if (mbedtls_md_hmac_finish(&ctx, out) != 0) {
    ret = -1;
    goto done;
  }
done:
  mbedtls_md_free(&ctx);
  return ret;
}

/*
 * Compute the PSK-keyed authentication HMAC over a handshake transcript.
 *
 * Transcript layout (single source of truth for both the client HELLO_ACK and
 * the server SESSION_OK HMACs — they differ only in `type` and the id field):
 *
 *   magic(2 LE) || version(1) || type(1) || nonce(32) || id_field(id_len) ||
 *   pub_c(32) || pub_s(32)
 *
 * `id_field` is the 6-byte device_id for the client HMAC and the 16-byte
 * session_id for the server HMAC.
 */
static int compute_transcript_hmac(const uint8_t* psk, size_t psk_len, uint8_t type,
                                   const uint8_t* nonce, const uint8_t* id_field, size_t id_len,
                                   const uint8_t* pub_c, const uint8_t* pub_s, uint8_t* out) {
  /* Buffer sized for the larger (session_id) variant; `off` is the true length. */
  uint8_t input[4 + USMP_NONCE_LEN + USMP_SESSION_ID_LEN + PUB_KEY_LEN + PUB_KEY_LEN];
  size_t off = 0;

  input[0] = (uint8_t)(USMP_MAGIC & 0xFF);
  input[1] = (uint8_t)((USMP_MAGIC >> 8) & 0xFF);
  input[2] = USMP_VERSION;
  input[3] = type;
  off = 4;
  memcpy(input + off, nonce, USMP_NONCE_LEN);
  off += USMP_NONCE_LEN;
  memcpy(input + off, id_field, id_len);
  off += id_len;
  memcpy(input + off, pub_c, PUB_KEY_LEN);
  off += PUB_KEY_LEN;
  memcpy(input + off, pub_s, PUB_KEY_LEN);
  off += PUB_KEY_LEN;

  return compute_hmac(psk, psk_len, input, off, out);
}

int usmp_handshake(usmp_transport_t* transport, usmp_t* session) {
  int ret = -1;
  char _msg[128];
  uint8_t k_c2s[USMP_SESSION_KEY_LEN] = {0};
  uint8_t k_s2c[USMP_SESSION_KEY_LEN] = {0};

  /*
   * Require an explicitly configured PSK of sufficient strength (>= 16 bytes).
   * Callers must set session->psk and session->psk_len before handshake.
   */
  if (!session->psk || session->psk_len < 16) {
    USMP_LOGE(TAG, "PSK must be configured and at least 16 bytes long");
    return -1;
  }
  const uint8_t* psk = session->psk;
  size_t psk_len = session->psk_len;

  mbedtls_ecdh_context ecdh;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;

  mbedtls_ecdh_init(&ecdh);
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctr_drbg);

  /*
   * Heap-allocate TX/RX buffers.
   * Stack allocation of 512-byte buffers is fatal on Arduino (256-512 bytes
   * total stack). Even on ESP32, the combined mbedtls context + two 512-byte
   * buffers exceeds comfortable stack limits for nested tasks.
   */
  uint8_t* tx_buf = (uint8_t*)malloc(512);
  uint8_t* rx_buf = (uint8_t*)malloc(512);
  if (!tx_buf || !rx_buf) {
    USMP_LOGE(TAG, "Out of memory for handshake buffers");
    free(tx_buf);
    free(rx_buf);
    return -1;
  }

  usmp_packet_t pkt;
  int len;

  /*
   * Seed RNG with a device-specific personalization string.
   * Using the device ID + a version tag gives ~48 bits of personalization
   * diversity across devices, reducing correlation between RNG streams.
   */
  uint8_t pers[USMP_DEVICE_ID_LEN + 8];
  if (usmp_port_get_device_id(pers, USMP_DEVICE_ID_LEN) != 0) {
    USMP_LOGE(TAG, "Failed to get device ID for RNG personalization");
    goto cleanup;
  }
  memcpy(pers + USMP_DEVICE_ID_LEN, "usmp-v1\x00", 8);

  if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, pers, sizeof(pers)) != 0) {
    USMP_LOGE(TAG, "RNG seed failed");
    goto cleanup;
  }

  // Setup Curve25519 ──────────────────────────────────────────────────────
  if (mbedtls_ecdh_setup(&ecdh, MBEDTLS_ECP_DP_CURVE25519) != 0) {
    USMP_LOGE(TAG, "ECDH setup failed");
    goto cleanup;
  }

  // Generate keypair + export public key ──────────────────────────────────
  uint8_t pub_buf[65];
  size_t pub_buf_len = 0;
  if (mbedtls_ecdh_make_public(&ecdh, &pub_buf_len, pub_buf, sizeof(pub_buf),
                               mbedtls_ctr_drbg_random, &ctr_drbg) != 0) {
    USMP_LOGE(TAG, "ECDH make_public failed");
    goto cleanup;
  }

  if (pub_buf_len < PUB_KEY_LEN) {
    USMP_LOGE(TAG, "Generated public key length is too short");
    goto cleanup;
  }
  uint8_t* pub_c = pub_buf + (pub_buf_len - PUB_KEY_LEN);

  // Step 1: Send HELLO [device_id(6) || pub_C(32)] ───────────────────────
  if (usmp_port_get_device_id(session->device_id, USMP_DEVICE_ID_LEN) != 0) {
    USMP_LOGE(TAG, "Failed to get device ID");
    goto cleanup;
  }

  memset(&pkt, 0, sizeof(pkt));
  pkt.magic = USMP_MAGIC;
  pkt.version = USMP_VERSION;
  pkt.type = USMP_TYPE_HELLO;
  pkt.seq = 0;
  pkt.length = USMP_DEVICE_ID_LEN + PUB_KEY_LEN;
  memcpy(pkt.payload, session->device_id, USMP_DEVICE_ID_LEN);
  memcpy(pkt.payload + USMP_DEVICE_ID_LEN, pub_c, PUB_KEY_LEN);

  len = usmp_build_packet(&pkt, tx_buf, NULL);
  if (transport->send(transport, tx_buf, (size_t)len) < 0) {
    USMP_LOGE(TAG, "Failed to send HELLO");
    goto cleanup;
  }
  /* Debug-only: avoid logging device_id at INFO level (information leakage) */
  USMP_LOGD(TAG, "HELLO sent");

  // Step 2: Receive CHALLENGE [nonce(32) || pub_S(32)] or HELLO_RETRY ────
  len = transport->recv(transport, rx_buf, 512);
  if (len < 0) {
    USMP_LOGE(TAG, "Failed to receive CHALLENGE");
    goto cleanup;
  }

  if (usmp_parse_packet(rx_buf, len, &pkt) != 0) {
    USMP_LOGE(TAG, "Failed to parse packet at step 2");
    goto cleanup;
  }

  if (pkt.type == USMP_TYPE_HELLO_RETRY) {
    if (pkt.length != USMP_COOKIE_LEN) {
      USMP_LOGE(TAG, "Bad HELLO_RETRY length");
      goto cleanup;
    }
    // Append cookie (which is in pkt.payload) to HELLO payload and send again
    memset(&pkt, 0, sizeof(pkt));
    pkt.magic = USMP_MAGIC;
    pkt.version = USMP_VERSION;
    pkt.type = USMP_TYPE_HELLO;
    /*
     * Distinct seq per handshake write (HELLO=0, cookie-retry HELLO=1,
     * HELLO_ACK=2). The retry HELLO is otherwise byte-identical to the initial
     * HELLO; on UDP the ARQ matches an (unauthenticated, plaintext) handshake
     * UTACK by (type, seq) only, so a shared seq lets a stale ACK for the first
     * HELLO satisfy the retry's wait — and lets an off-path attacker forge one.
     * Mirrors the Python client (Finding 13).
     */
    pkt.seq = 1;
    pkt.length = USMP_DEVICE_ID_LEN + PUB_KEY_LEN + USMP_COOKIE_LEN;
    memcpy(pkt.payload, session->device_id, USMP_DEVICE_ID_LEN);
    memcpy(pkt.payload + USMP_DEVICE_ID_LEN, pub_c, PUB_KEY_LEN);
    // Copy cookie from previous packet's payload
    memcpy(pkt.payload + USMP_DEVICE_ID_LEN + PUB_KEY_LEN, rx_buf + USMP_HEADER_SIZE,
           USMP_COOKIE_LEN);

    len = usmp_build_packet(&pkt, tx_buf, NULL);
    if (transport->send(transport, tx_buf, (size_t)len) < 0) {
      USMP_LOGE(TAG, "Failed to resend HELLO with cookie");
      goto cleanup;
    }

    // Read the actual CHALLENGE packet now
    len = transport->recv(transport, rx_buf, 512);
    if (len < 0) {
      USMP_LOGE(TAG, "Failed to receive CHALLENGE after cookie retry");
      goto cleanup;
    }

    if (usmp_parse_packet(rx_buf, len, &pkt) != 0) {
      USMP_LOGE(TAG, "Failed to parse CHALLENGE after cookie retry");
      goto cleanup;
    }
  }

  if (pkt.type != USMP_TYPE_CHALLENGE || pkt.length != USMP_NONCE_LEN + PUB_KEY_LEN) {
    snprintf(_msg, sizeof(_msg), "Bad CHALLENGE frame (type=0x%02x len=%u)", pkt.type, pkt.length);
    USMP_LOGE(TAG, _msg);
    goto cleanup;
  }

  uint8_t nonce[USMP_NONCE_LEN];
  uint8_t pub_s[PUB_KEY_LEN];
  memcpy(nonce, pkt.payload, USMP_NONCE_LEN);
  memcpy(pub_s, pkt.payload + USMP_NONCE_LEN, PUB_KEY_LEN);
  USMP_LOGI(TAG, "CHALLENGE received");

  // Compute X25519 shared secret ──────────────────────────────────────────
  /* peer_buf: 1-byte length prefix + 32-byte key (mbedtls X25519 format) */
  uint8_t peer_buf[1 + PUB_KEY_LEN];
  peer_buf[0] = PUB_KEY_LEN;
  memcpy(peer_buf + 1, pub_s, PUB_KEY_LEN);

  if (mbedtls_ecdh_read_public(&ecdh, peer_buf, sizeof(peer_buf)) != 0) {
    USMP_LOGE(TAG, "Failed to load server public key");
    goto cleanup;
  }

  uint8_t shared_secret[PUB_KEY_LEN];
  size_t shared_len = 0;
  if (mbedtls_ecdh_calc_secret(&ecdh, &shared_len, shared_secret, sizeof(shared_secret),
                               mbedtls_ctr_drbg_random, &ctr_drbg) != 0) {
    USMP_LOGE(TAG, "X25519 shared secret failed");
    goto cleanup;
  }
  USMP_LOGI(TAG, "X25519 shared secret computed");

  // L1 fix: reject all-zero shared secret (low-order point)
  {
    uint8_t zero_buf[PUB_KEY_LEN] = {0};
    if (mbedtls_ct_memcmp(shared_secret, zero_buf, shared_len) == 0) {
      USMP_LOGE(TAG, "X25519 produced all-zero shared secret (low-order point)");
      goto cleanup;
    }
  }

  // Derive directional session keys ────────────────────────────────────────
  if (derive_session_keys(shared_secret, shared_len, nonce, USMP_NONCE_LEN, pub_c, pub_s, k_c2s,
                          k_s2c) != 0) {
    USMP_LOGE(TAG, "HKDF failed");
    goto cleanup;
  }
  /* Client: encrypts with k_c2s, decrypts with k_s2c */
  memcpy(session->tx_key, k_c2s, USMP_SESSION_KEY_LEN);
  memcpy(session->rx_key, k_s2c, USMP_SESSION_KEY_LEN);
  USMP_LOGI(TAG, "Directional session keys derived");

  // Step 3: Send HELLO_ACK [hmac_client(32)] ─────────────────────────────
  uint8_t hmac_client[USMP_HMAC_LEN];
  if (compute_transcript_hmac(psk, psk_len, USMP_TYPE_HELLO_ACK, nonce, session->device_id,
                              USMP_DEVICE_ID_LEN, pub_c, pub_s, hmac_client) != 0) {
    USMP_LOGE(TAG, "Client HMAC computation failed");
    goto cleanup;
  }

  memset(&pkt, 0, sizeof(pkt));
  pkt.magic = USMP_MAGIC;
  pkt.version = USMP_VERSION;
  pkt.type = USMP_TYPE_HELLO_ACK;
  pkt.seq = 2;  // distinct handshake-write seq; see the cookie-retry note above (Finding 13)
  pkt.length = USMP_HMAC_LEN;
  memcpy(pkt.payload, hmac_client, USMP_HMAC_LEN);

  len = usmp_build_packet(&pkt, tx_buf, NULL);
  if (transport->send(transport, tx_buf, (size_t)len) < 0) {
    USMP_LOGE(TAG, "Failed to send HELLO_ACK");
    goto cleanup;
  }
  USMP_LOGI(TAG, "HELLO_ACK sent");

  // Step 4: Receive SESSION_OK [session_id(16) || hmac_server(32)] ────────
  len = transport->recv(transport, rx_buf, 512);
  if (len < 0) {
    USMP_LOGE(TAG, "Failed to receive SESSION_OK");
    goto cleanup;
  }

  if (usmp_parse_packet(rx_buf, len, &pkt) != 0 || pkt.type != USMP_TYPE_SESSION_OK ||
      pkt.length != USMP_SESSION_ID_LEN + USMP_HMAC_LEN) {
    snprintf(_msg, sizeof(_msg), "Bad SESSION_OK frame (type=0x%02x len=%u)", pkt.type, pkt.length);
    USMP_LOGE(TAG, _msg);
    goto cleanup;
  }

  uint8_t hmac_server_received[USMP_HMAC_LEN];
  memcpy(session->session_id, pkt.payload, USMP_SESSION_ID_LEN);
  memcpy(hmac_server_received, pkt.payload + USMP_SESSION_ID_LEN, USMP_HMAC_LEN);

  // Verify server HMAC ────────────────────────────────────────────────────
  uint8_t hmac_server_expected[USMP_HMAC_LEN];
  if (compute_transcript_hmac(psk, psk_len, USMP_TYPE_SESSION_OK, nonce, session->session_id,
                              USMP_SESSION_ID_LEN, pub_c, pub_s, hmac_server_expected) != 0) {
    USMP_LOGE(TAG, "Server HMAC computation failed");
    goto cleanup;
  }

  if (mbedtls_ct_memcmp(hmac_server_received, hmac_server_expected, USMP_HMAC_LEN) != 0) {
    USMP_LOGE(TAG, "Server HMAC verification FAILED — possible rogue server");
    goto cleanup;
  }

  USMP_LOGI(TAG, "Server authenticated OK");
  session->established = true;
  ret = 0;
  USMP_LOGI(TAG, "SESSION_OK — session established");

cleanup:
  /*
   * Securely wipe all sensitive material from memory.
   * mbedtls_platform_zeroize() is compiler-barrier-safe — unlike plain
   * memset(), it will not be optimized away even when the buffer goes
   * out of scope immediately after.
   */
  mbedtls_platform_zeroize(shared_secret, sizeof(shared_secret));
  mbedtls_platform_zeroize(hmac_client, sizeof(hmac_client));
  mbedtls_platform_zeroize(hmac_server_expected, sizeof(hmac_server_expected));
  mbedtls_platform_zeroize(hmac_server_received, sizeof(hmac_server_received));
  mbedtls_platform_zeroize(nonce, sizeof(nonce));
  mbedtls_platform_zeroize(pers, sizeof(pers));
  mbedtls_platform_zeroize(k_c2s, sizeof(k_c2s));
  mbedtls_platform_zeroize(k_s2c, sizeof(k_s2c));
  if (ret != 0) {
    /* Only wipe derived keys on failure — on success they're needed */
    mbedtls_platform_zeroize(session->tx_key, sizeof(session->tx_key));
    mbedtls_platform_zeroize(session->rx_key, sizeof(session->rx_key));
  }

  mbedtls_ecdh_free(&ecdh);
  mbedtls_entropy_free(&entropy);
  mbedtls_ctr_drbg_free(&ctr_drbg);

  if (tx_buf) {
    mbedtls_platform_zeroize(tx_buf, 512);
    free(tx_buf);
  }
  if (rx_buf) {
    mbedtls_platform_zeroize(rx_buf, 512);
    free(rx_buf);
  }

  return ret;
}
