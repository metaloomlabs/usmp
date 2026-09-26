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

static int usmp_mbedtls_entropy_callback(void* data, unsigned char* output, size_t len, size_t* olen) {
  (void)data;
  if (usmp_port_random(output, len) == 0) {
    *olen = len;
    return 0;
  }
  return -1;
}

typedef struct usmp_handshake_ctx {
  mbedtls_ecdh_context ecdh;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  uint8_t pub_c[PUB_KEY_LEN];
  uint8_t pub_s[PUB_KEY_LEN];
  uint8_t nonce[USMP_NONCE_LEN];
  uint8_t k_c2s[USMP_SESSION_KEY_LEN];
  uint8_t k_s2c[USMP_SESSION_KEY_LEN];
  uint8_t* tx_buf;
  uint8_t* rx_buf;
  bool allocated_buffers;
  bool allocated_ctx;
  uint32_t state_start_ms;
  uint32_t timeout_ms;
  bool cookie_retried;
} usmp_handshake_ctx_t;

void usmp_handshake_abort(usmp_transport_t* transport, usmp_t* session) {
  (void)transport;
  if (!session || !session->hs_ctx) return;
  usmp_handshake_ctx_t* hs = (usmp_handshake_ctx_t*)session->hs_ctx;
  mbedtls_ecdh_free(&hs->ecdh);
  mbedtls_entropy_free(&hs->entropy);
  mbedtls_ctr_drbg_free(&hs->ctr_drbg);
  if (hs->tx_buf) {
    mbedtls_platform_zeroize(hs->tx_buf, 512);
    if (hs->allocated_buffers) free(hs->tx_buf);
  }
  if (hs->rx_buf) {
    mbedtls_platform_zeroize(hs->rx_buf, 512);
    if (hs->allocated_buffers) free(hs->rx_buf);
  }
  bool alloc_ctx = hs->allocated_ctx;
  mbedtls_platform_zeroize(hs, sizeof(usmp_handshake_ctx_t));
  if (alloc_ctx) {
    free(hs);
  }
  session->hs_ctx = NULL;
}

usmp_err_t usmp_handshake_start(usmp_transport_t* transport, usmp_t* session) {
  if (!session || !transport) return USMP_ERR_INVALID_ARG;
  if (!session->psk || session->psk_len < 16) {
    USMP_LOGE(TAG, "PSK must be configured and at least 16 bytes long");
    session->state = USMP_STATE_ERROR;
    return USMP_ERR_INVALID_ARG;
  }
  if (!transport->send || !transport->recv) {
    USMP_LOGE(TAG, "Transport missing send or recv");
    session->state = USMP_STATE_ERROR;
    return USMP_ERR_INVALID_ARG;
  }

  if (session->hs_ctx) {
    usmp_handshake_abort(transport, session);
  }

  usmp_handshake_ctx_t* hs = NULL;
  bool alloc_ctx = false;
  bool alloc_buffers = false;
  uint8_t* tx_buf = NULL;
  uint8_t* rx_buf = NULL;

  /* Check if caller provided sufficient scratch buffer */
  if (session->scratch && session->scratch_len >= (sizeof(usmp_handshake_ctx_t) + 1024)) {
    hs = (usmp_handshake_ctx_t*)session->scratch;
    memset(hs, 0, sizeof(usmp_handshake_ctx_t));
    tx_buf = session->scratch + sizeof(usmp_handshake_ctx_t);
    rx_buf = session->scratch + sizeof(usmp_handshake_ctx_t) + 512;
    alloc_ctx = false;
    alloc_buffers = false;
  } else if (session->scratch && session->scratch_len >= USMP_HANDSHAKE_SCRATCH_LEN) {
#ifdef USMP_ZERO_HEAP
    USMP_LOGE(TAG, "Zero-heap mode requires scratch buffer >= %zu bytes",
              sizeof(usmp_handshake_ctx_t) + 1024);
    session->state = USMP_STATE_ERROR;
    return USMP_ERR_BUFFER_OVERFLOW;
#else
    hs = (usmp_handshake_ctx_t*)calloc(1, sizeof(usmp_handshake_ctx_t));
    if (!hs) {
      session->state = USMP_STATE_ERROR;
      return USMP_ERR_BUFFER_OVERFLOW;
    }
    tx_buf = session->scratch;
    rx_buf = session->scratch + 512;
    alloc_ctx = true;
    alloc_buffers = false;
#endif
  } else {
#ifdef USMP_ZERO_HEAP
    USMP_LOGE(TAG, "Zero-heap mode requires scratch buffer >= %zu bytes",
              sizeof(usmp_handshake_ctx_t) + 1024);
    session->state = USMP_STATE_ERROR;
    return USMP_ERR_BUFFER_OVERFLOW;
#else
    hs = (usmp_handshake_ctx_t*)calloc(1, sizeof(usmp_handshake_ctx_t));
    if (!hs) {
      session->state = USMP_STATE_ERROR;
      return USMP_ERR_BUFFER_OVERFLOW;
    }
    tx_buf = (uint8_t*)malloc(512);
    rx_buf = (uint8_t*)malloc(512);
    if (!tx_buf || !rx_buf) {
      free(tx_buf);
      free(rx_buf);
      free(hs);
      session->state = USMP_STATE_ERROR;
      return USMP_ERR_BUFFER_OVERFLOW;
    }
    alloc_ctx = true;
    alloc_buffers = true;
#endif
  }

  hs->tx_buf = tx_buf;
  hs->rx_buf = rx_buf;
  hs->allocated_ctx = alloc_ctx;
  hs->allocated_buffers = alloc_buffers;
  hs->timeout_ms = 5000;  // 5 seconds default per state

  mbedtls_ecdh_init(&hs->ecdh);
  mbedtls_entropy_init(&hs->entropy);
  mbedtls_ctr_drbg_init(&hs->ctr_drbg);

  mbedtls_entropy_add_source(&hs->entropy, usmp_mbedtls_entropy_callback, NULL, 32,
                             MBEDTLS_ENTROPY_SOURCE_STRONG);

  uint8_t pers[USMP_DEVICE_ID_LEN + 8];
  if (usmp_port_get_device_id(pers, USMP_DEVICE_ID_LEN) != 0) {
    USMP_LOGE(TAG, "Failed to get device ID for RNG personalization");
    session->hs_ctx = hs;
    usmp_handshake_abort(transport, session);
    session->state = USMP_STATE_ERROR;
    return USMP_ERR_INVALID_ARG;
  }
  memcpy(pers + USMP_DEVICE_ID_LEN, "usmp-v1\x00", 8);

  if (mbedtls_ctr_drbg_seed(&hs->ctr_drbg, mbedtls_entropy_func, &hs->entropy, pers,
                            sizeof(pers)) != 0) {
    USMP_LOGE(TAG, "RNG seed failed");
    session->hs_ctx = hs;
    usmp_handshake_abort(transport, session);
    session->state = USMP_STATE_ERROR;
    return USMP_ERR_CRYPTO_FAILED;
  }

  if (mbedtls_ecdh_setup(&hs->ecdh, MBEDTLS_ECP_DP_CURVE25519) != 0) {
    USMP_LOGE(TAG, "ECDH setup failed");
    session->hs_ctx = hs;
    usmp_handshake_abort(transport, session);
    session->state = USMP_STATE_ERROR;
    return USMP_ERR_CRYPTO_FAILED;
  }

  uint8_t pub_buf[65];
  size_t pub_buf_len = 0;
  if (mbedtls_ecdh_make_public(&hs->ecdh, &pub_buf_len, pub_buf, sizeof(pub_buf),
                               mbedtls_ctr_drbg_random, &hs->ctr_drbg) != 0 ||
      pub_buf_len < PUB_KEY_LEN) {
    USMP_LOGE(TAG, "ECDH make_public failed");
    session->hs_ctx = hs;
    usmp_handshake_abort(transport, session);
    session->state = USMP_STATE_ERROR;
    return USMP_ERR_CRYPTO_FAILED;
  }
  memcpy(hs->pub_c, pub_buf + (pub_buf_len - PUB_KEY_LEN), PUB_KEY_LEN);

  if (usmp_port_get_device_id(session->device_id, USMP_DEVICE_ID_LEN) != 0) {
    USMP_LOGE(TAG, "Failed to get device ID");
    session->hs_ctx = hs;
    usmp_handshake_abort(transport, session);
    session->state = USMP_STATE_ERROR;
    return USMP_ERR_INVALID_ARG;
  }

  usmp_packet_t pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.magic = USMP_MAGIC;
  pkt.version = USMP_VERSION;
  pkt.type = USMP_TYPE_HELLO;
  pkt.seq = 0;
  pkt.length = USMP_DEVICE_ID_LEN + PUB_KEY_LEN;
  memcpy(pkt.payload, session->device_id, USMP_DEVICE_ID_LEN);
  memcpy(pkt.payload + USMP_DEVICE_ID_LEN, hs->pub_c, PUB_KEY_LEN);

  int len = usmp_build_packet(&pkt, hs->tx_buf, NULL);
  if (transport->send(transport, hs->tx_buf, (size_t)len) < 0) {
    USMP_LOGE(TAG, "Failed to send HELLO");
    session->hs_ctx = hs;
    usmp_handshake_abort(transport, session);
    session->state = USMP_STATE_ERROR;
    return USMP_ERR_TRANSPORT_FAILED;
  }
  USMP_LOGD(TAG, "HELLO sent");

  hs->state_start_ms = usmp_port_millis();
  session->hs_ctx = hs;
  session->state = USMP_STATE_AWAITING_CHALLENGE;
  return USMP_OK;
}

usmp_err_t usmp_handshake_step(usmp_transport_t* transport, usmp_t* session) {
  if (!session || !transport) return USMP_ERR_INVALID_ARG;
  if (session->state == USMP_STATE_ESTABLISHED) return USMP_OK;
  if (session->state == USMP_STATE_IDLE) return USMP_ERR_NOT_CONNECTED;
  if (session->state == USMP_STATE_ERROR || !session->hs_ctx) return USMP_ERR_AUTH_FAILED;

  usmp_handshake_ctx_t* hs = (usmp_handshake_ctx_t*)session->hs_ctx;
  uint32_t now = usmp_port_millis();
  char _msg[128];

  if (session->state == USMP_STATE_AWAITING_CHALLENGE) {
    if ((now - hs->state_start_ms) >= hs->timeout_ms) {
      USMP_LOGE(TAG, "Timeout waiting for CHALLENGE");
      session->state = USMP_STATE_ERROR;
      usmp_handshake_abort(transport, session);
      return USMP_ERR_TIMEOUT;
    }

    if (transport->available && transport->available(transport) <= 0) {
      return USMP_OK;  // No frame ready yet; return non-blockingly
    }

    int len = transport->recv(transport, hs->rx_buf, 512);
    if (len == 0) {
      return USMP_OK;  // Non-blocking empty read
    }
    if (len < 0) {
      USMP_LOGE(TAG, "Failed to receive CHALLENGE");
      session->state = USMP_STATE_ERROR;
      usmp_handshake_abort(transport, session);
      return USMP_ERR_TRANSPORT_FAILED;
    }

    usmp_packet_t pkt;
    if (usmp_parse_packet(hs->rx_buf, len, &pkt) != 0) {
      USMP_LOGE(TAG, "Failed to parse packet at step 2");
      session->state = USMP_STATE_ERROR;
      usmp_handshake_abort(transport, session);
      return USMP_ERR_AUTH_FAILED;
    }

    if (pkt.type == USMP_TYPE_HELLO_RETRY) {
      if (pkt.length != USMP_COOKIE_LEN || hs->cookie_retried) {
        USMP_LOGE(TAG, "Bad HELLO_RETRY length or repeated retry");
        session->state = USMP_STATE_ERROR;
        usmp_handshake_abort(transport, session);
        return USMP_ERR_AUTH_FAILED;
      }
      hs->cookie_retried = true;
      memset(&pkt, 0, sizeof(pkt));
      pkt.magic = USMP_MAGIC;
      pkt.version = USMP_VERSION;
      pkt.type = USMP_TYPE_HELLO;
      pkt.seq = 1;
      pkt.length = USMP_DEVICE_ID_LEN + PUB_KEY_LEN + USMP_COOKIE_LEN;
      memcpy(pkt.payload, session->device_id, USMP_DEVICE_ID_LEN);
      memcpy(pkt.payload + USMP_DEVICE_ID_LEN, hs->pub_c, PUB_KEY_LEN);
      memcpy(pkt.payload + USMP_DEVICE_ID_LEN + PUB_KEY_LEN, hs->rx_buf + USMP_HEADER_SIZE,
             USMP_COOKIE_LEN);

      int slen = usmp_build_packet(&pkt, hs->tx_buf, NULL);
      if (transport->send(transport, hs->tx_buf, (size_t)slen) < 0) {
        USMP_LOGE(TAG, "Failed to resend HELLO with cookie");
        session->state = USMP_STATE_ERROR;
        usmp_handshake_abort(transport, session);
        return USMP_ERR_TRANSPORT_FAILED;
      }
      hs->state_start_ms = usmp_port_millis();
      return USMP_OK;
    }

    if (pkt.type != USMP_TYPE_CHALLENGE || pkt.length != USMP_NONCE_LEN + PUB_KEY_LEN) {
      snprintf(_msg, sizeof(_msg), "Bad CHALLENGE frame (type=0x%02x len=%u)", pkt.type, pkt.length);
      USMP_LOGE(TAG, _msg);
      session->state = USMP_STATE_ERROR;
      usmp_handshake_abort(transport, session);
      return USMP_ERR_AUTH_FAILED;
    }

    memcpy(hs->nonce, pkt.payload, USMP_NONCE_LEN);
    memcpy(hs->pub_s, pkt.payload + USMP_NONCE_LEN, PUB_KEY_LEN);
    USMP_LOGI(TAG, "CHALLENGE received");

    session->state = USMP_STATE_CALCULATING_ECDH;
    hs->state_start_ms = usmp_port_millis();
    /* Fall through immediately to compute ECDH without waiting for next loop tick */
  }

  if (session->state == USMP_STATE_CALCULATING_ECDH) {
    uint8_t peer_buf[1 + PUB_KEY_LEN];
    peer_buf[0] = PUB_KEY_LEN;
    memcpy(peer_buf + 1, hs->pub_s, PUB_KEY_LEN);

    if (mbedtls_ecdh_read_public(&hs->ecdh, peer_buf, sizeof(peer_buf)) != 0) {
      USMP_LOGE(TAG, "Failed to load server public key");
      session->state = USMP_STATE_ERROR;
      usmp_handshake_abort(transport, session);
      return USMP_ERR_CRYPTO_FAILED;
    }

    uint8_t shared_secret[PUB_KEY_LEN];
    size_t shared_len = 0;
    if (mbedtls_ecdh_calc_secret(&hs->ecdh, &shared_len, shared_secret, sizeof(shared_secret),
                                 mbedtls_ctr_drbg_random, &hs->ctr_drbg) != 0) {
      USMP_LOGE(TAG, "X25519 shared secret failed");
      session->state = USMP_STATE_ERROR;
      usmp_handshake_abort(transport, session);
      return USMP_ERR_CRYPTO_FAILED;
    }
    USMP_LOGI(TAG, "X25519 shared secret computed");

    uint8_t zero_buf[PUB_KEY_LEN] = {0};
    if (mbedtls_ct_memcmp(shared_secret, zero_buf, shared_len) == 0) {
      USMP_LOGE(TAG, "X25519 produced all-zero shared secret (low-order point)");
      mbedtls_platform_zeroize(shared_secret, sizeof(shared_secret));
      session->state = USMP_STATE_ERROR;
      usmp_handshake_abort(transport, session);
      return USMP_ERR_CRYPTO_FAILED;
    }

    if (derive_session_keys(shared_secret, shared_len, hs->nonce, USMP_NONCE_LEN, hs->pub_c,
                            hs->pub_s, hs->k_c2s, hs->k_s2c) != 0) {
      USMP_LOGE(TAG, "HKDF failed");
      mbedtls_platform_zeroize(shared_secret, sizeof(shared_secret));
      session->state = USMP_STATE_ERROR;
      usmp_handshake_abort(transport, session);
      return USMP_ERR_CRYPTO_FAILED;
    }
    mbedtls_platform_zeroize(shared_secret, sizeof(shared_secret));

    memcpy(session->tx_key, hs->k_c2s, USMP_SESSION_KEY_LEN);
    memcpy(session->rx_key, hs->k_s2c, USMP_SESSION_KEY_LEN);
    USMP_LOGI(TAG, "Directional session keys derived");

    uint8_t hmac_client[USMP_HMAC_LEN];
    if (compute_transcript_hmac(session->psk, session->psk_len, USMP_TYPE_HELLO_ACK, hs->nonce,
                                session->device_id, USMP_DEVICE_ID_LEN, hs->pub_c, hs->pub_s,
                                hmac_client) != 0) {
      USMP_LOGE(TAG, "Client HMAC computation failed");
      session->state = USMP_STATE_ERROR;
      usmp_handshake_abort(transport, session);
      return USMP_ERR_CRYPTO_FAILED;
    }

    usmp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.magic = USMP_MAGIC;
    pkt.version = USMP_VERSION;
    pkt.type = USMP_TYPE_HELLO_ACK;
    pkt.seq = 2;
    pkt.length = USMP_HMAC_LEN;
    memcpy(pkt.payload, hmac_client, USMP_HMAC_LEN);
    mbedtls_platform_zeroize(hmac_client, sizeof(hmac_client));

    int len = usmp_build_packet(&pkt, hs->tx_buf, NULL);
    if (transport->send(transport, hs->tx_buf, (size_t)len) < 0) {
      USMP_LOGE(TAG, "Failed to send HELLO_ACK");
      session->state = USMP_STATE_ERROR;
      usmp_handshake_abort(transport, session);
      return USMP_ERR_TRANSPORT_FAILED;
    }
    USMP_LOGI(TAG, "HELLO_ACK sent");

    session->state = USMP_STATE_AWAITING_SESSION_OK;
    hs->state_start_ms = usmp_port_millis();
    return USMP_OK;
  }

  if (session->state == USMP_STATE_AWAITING_SESSION_OK) {
    if ((now - hs->state_start_ms) >= hs->timeout_ms) {
      USMP_LOGE(TAG, "Timeout waiting for SESSION_OK");
      session->state = USMP_STATE_ERROR;
      usmp_handshake_abort(transport, session);
      return USMP_ERR_TIMEOUT;
    }

    if (transport->available && transport->available(transport) <= 0) {
      return USMP_OK;  // No frame ready yet; return non-blockingly
    }

    int len = transport->recv(transport, hs->rx_buf, 512);
    if (len == 0) {
      return USMP_OK;  // Non-blocking empty read
    }
    if (len < 0) {
      USMP_LOGE(TAG, "Failed to receive SESSION_OK");
      session->state = USMP_STATE_ERROR;
      usmp_handshake_abort(transport, session);
      return USMP_ERR_TRANSPORT_FAILED;
    }

    usmp_packet_t pkt;
    if (usmp_parse_packet(hs->rx_buf, len, &pkt) != 0 || pkt.type != USMP_TYPE_SESSION_OK ||
        pkt.length != USMP_SESSION_ID_LEN + USMP_HMAC_LEN) {
      snprintf(_msg, sizeof(_msg), "Bad SESSION_OK frame (type=0x%02x len=%u)", pkt.type, pkt.length);
      USMP_LOGE(TAG, _msg);
      session->state = USMP_STATE_ERROR;
      usmp_handshake_abort(transport, session);
      return USMP_ERR_AUTH_FAILED;
    }

    uint8_t hmac_server_received[USMP_HMAC_LEN];
    memcpy(session->session_id, pkt.payload, USMP_SESSION_ID_LEN);
    memcpy(hmac_server_received, pkt.payload + USMP_SESSION_ID_LEN, USMP_HMAC_LEN);

    uint8_t hmac_server_expected[USMP_HMAC_LEN];
    if (compute_transcript_hmac(session->psk, session->psk_len, USMP_TYPE_SESSION_OK, hs->nonce,
                                session->session_id, USMP_SESSION_ID_LEN, hs->pub_c, hs->pub_s,
                                hmac_server_expected) != 0) {
      USMP_LOGE(TAG, "Server HMAC computation failed");
      session->state = USMP_STATE_ERROR;
      usmp_handshake_abort(transport, session);
      return USMP_ERR_CRYPTO_FAILED;
    }

    if (mbedtls_ct_memcmp(hmac_server_received, hmac_server_expected, USMP_HMAC_LEN) != 0) {
      USMP_LOGE(TAG, "Server HMAC verification FAILED — possible rogue server");
      mbedtls_platform_zeroize(hmac_server_expected, sizeof(hmac_server_expected));
      mbedtls_platform_zeroize(hmac_server_received, sizeof(hmac_server_received));
      session->state = USMP_STATE_ERROR;
      usmp_handshake_abort(transport, session);
      return USMP_ERR_AUTH_FAILED;
    }
    mbedtls_platform_zeroize(hmac_server_expected, sizeof(hmac_server_expected));
    mbedtls_platform_zeroize(hmac_server_received, sizeof(hmac_server_received));

    USMP_LOGI(TAG, "Server authenticated OK");
    if (transport->set_session_keys) {
      transport->set_session_keys(transport, session->tx_key, session->rx_key);
    }
    session->established = true;
    session->tx_seq = 0;
    session->rx_seq = 0;
    session->rx_window_bitmap = 0;
    session->last_tx_ms = usmp_port_millis();
    session->state = USMP_STATE_ESTABLISHED;

    usmp_handshake_abort(transport, session);
    USMP_LOGI(TAG, "SESSION_OK — session established");
    return USMP_OK;
  }

  return USMP_OK;
}

usmp_err_t usmp_handshake(usmp_transport_t* transport, usmp_t* session) {
  usmp_err_t ret = usmp_handshake_start(transport, session);
  if (ret != USMP_OK) return ret;

  while (session->state != USMP_STATE_ESTABLISHED && session->state != USMP_STATE_ERROR) {
    usmp_port_wdt_feed();
    ret = usmp_handshake_step(transport, session);
    if (ret != USMP_OK) {
      return ret;
    }
    if (session->state != USMP_STATE_ESTABLISHED) {
      usmp_port_delay_ms(2);
    }
  }

  if (session->state == USMP_STATE_ESTABLISHED) {
    return USMP_OK;
  }
  return USMP_ERR_AUTH_FAILED;
}
