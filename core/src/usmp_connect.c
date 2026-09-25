#include <stdio.h>
#include <string.h>

#include "mbedtls/platform_util.h"
#include "usmp.h"
#include "usmp_frame.h"
#include "usmp_handshake.h"
#include "usmp_port.h"
#include "usmp_session.h"
#include "usmp_transport.h"

static const char* TAG = "USMP";

/* Log `prefix` followed by the 16-byte session id as lowercase hex. */
static void log_session_id(const char* prefix, const uint8_t* session_id) {
  char _msg[128];
  snprintf(_msg, sizeof(_msg),
           "%s%02x%02x%02x%02x%02x%02x%02x%02x"
           "%02x%02x%02x%02x%02x%02x%02x%02x",
           prefix, session_id[0], session_id[1], session_id[2], session_id[3], session_id[4],
           session_id[5], session_id[6], session_id[7], session_id[8], session_id[9],
           session_id[10], session_id[11], session_id[12], session_id[13], session_id[14],
           session_id[15]);
  USMP_LOGI(TAG, _msg);
}

usmp_err_t usmp_connect(usmp_t* ctx, usmp_transport_t* transport) {
  if (!ctx || !transport) return USMP_ERR_INVALID_ARG;

  /* Validate required transport function pointers before use */
  if (!transport->send || !transport->recv) {
    USMP_LOGE(TAG, "Transport missing send or recv — cannot connect");
    return USMP_ERR_INVALID_ARG;
  }

  const uint8_t* psk = ctx->psk;
  size_t psk_len = ctx->psk_len;
  uint32_t keepalive_ms = ctx->keepalive_ms;
  uint8_t* scratch = ctx->scratch;
  size_t scratch_len = ctx->scratch_len;

  if (ctx->tx_mutex) {
    usmp_port_mutex_destroy(ctx->tx_mutex);
    ctx->tx_mutex = NULL;
  }
  if (ctx->rx_mutex) {
    usmp_port_mutex_destroy(ctx->rx_mutex);
    ctx->rx_mutex = NULL;
  }

  memset(ctx, 0, sizeof(usmp_t));

  ctx->transport = *transport;
  ctx->psk = psk;
  ctx->psk_len = psk_len;
  ctx->keepalive_ms = keepalive_ms;
  ctx->scratch = scratch;
  ctx->scratch_len = scratch_len;

  if (usmp_port_mutex_create(&ctx->tx_mutex) != 0 ||
      usmp_port_mutex_create(&ctx->rx_mutex) != 0) {
    USMP_LOGE(TAG, "Failed to create session mutexes");
    if (ctx->tx_mutex) {
      usmp_port_mutex_destroy(ctx->tx_mutex);
      ctx->tx_mutex = NULL;
    }
    if (ctx->rx_mutex) {
      usmp_port_mutex_destroy(ctx->rx_mutex);
      ctx->rx_mutex = NULL;
    }
    return USMP_ERR_MUTEX_FAILED;
  }

  usmp_t hs = {0};
  hs.psk = ctx->psk;
  hs.psk_len = ctx->psk_len;
  hs.scratch = ctx->scratch;
  hs.scratch_len = ctx->scratch_len;

  usmp_err_t ret = USMP_OK;
  usmp_err_t hs_ret = usmp_handshake(&ctx->transport, &hs);
  if (hs_ret != USMP_OK) {
    USMP_LOGE(TAG, "Handshake failed");
    if (ctx->transport.close) ctx->transport.close(&ctx->transport);
    if (ctx->tx_mutex) {
      usmp_port_mutex_destroy(ctx->tx_mutex);
      ctx->tx_mutex = NULL;
    }
    if (ctx->rx_mutex) {
      usmp_port_mutex_destroy(ctx->rx_mutex);
      ctx->rx_mutex = NULL;
    }
    ret = hs_ret;
    goto cleanup;
  }

  memcpy(ctx->device_id, hs.device_id, USMP_DEVICE_ID_LEN);
  memcpy(ctx->session_id, hs.session_id, USMP_SESSION_ID_LEN);
  memcpy(ctx->tx_key, hs.tx_key, USMP_SESSION_KEY_LEN);
  memcpy(ctx->rx_key, hs.rx_key, USMP_SESSION_KEY_LEN);
  /* S3: hand the derived keys to the transport so session-phase UTACKs are
     authenticated. Synchronous client — keys are set before any session I/O. */
  if (ctx->transport.set_session_keys) {
    ctx->transport.set_session_keys(&ctx->transport, ctx->tx_key, ctx->rx_key);
  }
  ctx->established = true;
  ctx->tx_seq = 0;
  ctx->rx_seq = 0;
  ctx->last_tx_ms = usmp_port_millis();

  log_session_id("Session established — id: ", ctx->session_id);
  ret = USMP_OK;

cleanup:
  mbedtls_platform_zeroize(&hs, sizeof(hs));
  return ret;
}

usmp_err_t usmp_reconnect(usmp_t* ctx) {
  if (!ctx) return USMP_ERR_INVALID_ARG;

  // Strict hierarchical locking: tx_mutex first, then rx_mutex
  if (usmp_port_mutex_lock(ctx->tx_mutex) != 0) return USMP_ERR_MUTEX_FAILED;
  if (usmp_port_mutex_lock(ctx->rx_mutex) != 0) {
    usmp_port_mutex_unlock(ctx->tx_mutex);
    return USMP_ERR_MUTEX_FAILED;
  }

  /* Zeroise the old session key immediately on entering reconnect */
  mbedtls_platform_zeroize(ctx->tx_key, sizeof(ctx->tx_key));
  mbedtls_platform_zeroize(ctx->rx_key, sizeof(ctx->rx_key));

  if (!ctx->transport.reconnect) {
    USMP_LOGE(TAG, "Transport does not support reconnect");
    usmp_port_mutex_unlock(ctx->rx_mutex);
    usmp_port_mutex_unlock(ctx->tx_mutex);
    return USMP_ERR_TRANSPORT_FAILED;
  }

  ctx->established = false;

  //  Re-dial transport ─────────────────────────────────────────────────────
  if (ctx->transport.reconnect(&ctx->transport) != 0) {
    USMP_LOGE(TAG, "Transport reconnect failed");
    usmp_port_mutex_unlock(ctx->rx_mutex);
    usmp_port_mutex_unlock(ctx->tx_mutex);
    return USMP_ERR_TRANSPORT_FAILED;
  }
  USMP_LOGI(TAG, "Transport reconnected — starting handshake");

  // Full new handshake ────────────────────────────────────────────────────
  usmp_t hs = {0};
  hs.psk = ctx->psk;
  hs.psk_len = ctx->psk_len;
  hs.scratch = ctx->scratch;
  hs.scratch_len = ctx->scratch_len;

  usmp_err_t ret = USMP_OK;
  usmp_err_t hs_ret = usmp_handshake(&ctx->transport, &hs);
  if (hs_ret != USMP_OK) {
    USMP_LOGE(TAG, "Handshake failed after reconnect");
    if (ctx->transport.close) ctx->transport.close(&ctx->transport);
    ret = hs_ret;
    goto cleanup;
  }

  memcpy(ctx->session_id, hs.session_id, USMP_SESSION_ID_LEN);
  memcpy(ctx->tx_key, hs.tx_key, USMP_SESSION_KEY_LEN);
  memcpy(ctx->rx_key, hs.rx_key, USMP_SESSION_KEY_LEN);
  /* S3: hand the derived keys to the transport so session-phase UTACKs are
     authenticated. Synchronous client — keys are set before any session I/O. */
  if (ctx->transport.set_session_keys) {
    ctx->transport.set_session_keys(&ctx->transport, ctx->tx_key, ctx->rx_key);
  }
  ctx->established = true;
  ctx->tx_seq = 0;
  ctx->rx_seq = 0;
  ctx->rx_window_bitmap = 0;
  ctx->last_tx_ms = usmp_port_millis();

  log_session_id("Reconnected — new session: ", ctx->session_id);
  ret = USMP_OK;

cleanup:
  mbedtls_platform_zeroize(&hs, sizeof(hs));
  usmp_port_mutex_unlock(ctx->rx_mutex);
  usmp_port_mutex_unlock(ctx->tx_mutex);
  return ret;
}

void usmp_close(usmp_t* ctx) {
  if (!ctx) return;

  // Strict hierarchical locking: tx_mutex first, then rx_mutex
  usmp_port_mutex_lock(ctx->tx_mutex);
  usmp_port_mutex_lock(ctx->rx_mutex);

  /*
   * Courtesy BYE so the peer can release the session immediately instead of
   * holding it until its inactivity watchdog fires. Best-effort: the session is
   * over regardless, so an undelivered BYE is ignored. It must go out before we
   * tear down the transport or zeroise the tx_key it is encrypted under.
   */
  if (ctx->established) usmp_send_bye_locked(ctx);
  ctx->established = false;
  if (ctx->transport.close) ctx->transport.close(&ctx->transport);
  mbedtls_platform_zeroize(ctx->tx_key, sizeof(ctx->tx_key));
  mbedtls_platform_zeroize(ctx->rx_key, sizeof(ctx->rx_key));

  usmp_mutex_t tx_m = ctx->tx_mutex;
  usmp_mutex_t rx_m = ctx->rx_mutex;
  ctx->tx_mutex = NULL;
  ctx->rx_mutex = NULL;

  usmp_port_mutex_unlock(rx_m);
  usmp_port_mutex_unlock(tx_m);

  usmp_port_mutex_destroy(rx_m);
  usmp_port_mutex_destroy(tx_m);

  USMP_LOGI(TAG, "Session closed");
}

const char* usmp_get_version(void) { return "1.2.2"; }

static usmp_log_level_t g_usmp_log_level = USMP_LOG_LEVEL_ERROR;

void usmp_set_log_level(usmp_log_level_t level) { g_usmp_log_level = level; }

usmp_log_level_t usmp_get_log_level(void) { return g_usmp_log_level; }

static usmp_log_level_t level_char_to_enum(char level) {
  switch (level) {
    case 'E':
      return USMP_LOG_LEVEL_ERROR;
    case 'W':
      return USMP_LOG_LEVEL_WARN;
    case 'I':
      return USMP_LOG_LEVEL_INFO;
    case 'D':
      return USMP_LOG_LEVEL_DEBUG;
    default:
      return USMP_LOG_LEVEL_NONE;
  }
}

void usmp_log(char level, const char* tag, const char* msg) {
  if (level_char_to_enum(level) > g_usmp_log_level) {
    return;
  }
  usmp_port_log(level, tag, msg);
}
