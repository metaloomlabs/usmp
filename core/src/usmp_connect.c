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

usmp_err_t usmp_connect_async(usmp_t* ctx, usmp_transport_t* transport) {
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
  if (ctx->hs_ctx) {
    usmp_handshake_abort(&ctx->transport, ctx);
  }

  memset(ctx, 0, sizeof(usmp_t));

  ctx->transport = *transport;
  ctx->psk = psk;
  ctx->psk_len = psk_len;
  ctx->keepalive_ms = keepalive_ms;
  ctx->scratch = scratch;
  ctx->scratch_len = scratch_len;
  ctx->state = USMP_STATE_IDLE;

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
    ctx->state = USMP_STATE_ERROR;
    return USMP_ERR_MUTEX_FAILED;
  }

  usmp_err_t hs_ret = usmp_handshake_start(&ctx->transport, ctx);
  if (hs_ret != USMP_OK) {
    USMP_LOGE(TAG, "Failed to start handshake");
    if (ctx->transport.close) ctx->transport.close(&ctx->transport);
    if (ctx->tx_mutex) {
      usmp_port_mutex_destroy(ctx->tx_mutex);
      ctx->tx_mutex = NULL;
    }
    if (ctx->rx_mutex) {
      usmp_port_mutex_destroy(ctx->rx_mutex);
      ctx->rx_mutex = NULL;
    }
    ctx->state = USMP_STATE_ERROR;
    return hs_ret;
  }

  return USMP_OK;
}

usmp_err_t usmp_reconnect_async(usmp_t* ctx) {
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
    ctx->state = USMP_STATE_ERROR;
    usmp_port_mutex_unlock(ctx->rx_mutex);
    usmp_port_mutex_unlock(ctx->tx_mutex);
    return USMP_ERR_TRANSPORT_FAILED;
  }

  ctx->established = false;
  if (ctx->hs_ctx) {
    usmp_handshake_abort(&ctx->transport, ctx);
  }

  if (ctx->transport.reconnect(&ctx->transport) != 0) {
    USMP_LOGE(TAG, "Transport reconnect failed");
    ctx->state = USMP_STATE_ERROR;
    usmp_port_mutex_unlock(ctx->rx_mutex);
    usmp_port_mutex_unlock(ctx->tx_mutex);
    return USMP_ERR_TRANSPORT_FAILED;
  }
  USMP_LOGI(TAG, "Transport reconnected — starting handshake");

  usmp_err_t hs_ret = usmp_handshake_start(&ctx->transport, ctx);
  if (hs_ret != USMP_OK) {
    USMP_LOGE(TAG, "Handshake start failed after reconnect");
    if (ctx->transport.close) ctx->transport.close(&ctx->transport);
    ctx->state = USMP_STATE_ERROR;
    usmp_port_mutex_unlock(ctx->rx_mutex);
    usmp_port_mutex_unlock(ctx->tx_mutex);
    return hs_ret;
  }

  usmp_port_mutex_unlock(ctx->rx_mutex);
  usmp_port_mutex_unlock(ctx->tx_mutex);
  return USMP_OK;
}

usmp_err_t usmp_step(usmp_t* ctx) {
  if (!ctx) return USMP_ERR_INVALID_ARG;

  usmp_port_wdt_feed();

  switch (ctx->state) {
    case USMP_STATE_IDLE:
      return USMP_ERR_NOT_CONNECTED;

    case USMP_STATE_CONNECTING_SOCKET:
    case USMP_STATE_AWAITING_CHALLENGE:
    case USMP_STATE_CALCULATING_ECDH:
    case USMP_STATE_AWAITING_SESSION_OK: {
      usmp_err_t ret = usmp_handshake_step(&ctx->transport, ctx);
      if (ret != USMP_OK) {
        ctx->state = USMP_STATE_ERROR;
        if (ctx->transport.close) ctx->transport.close(&ctx->transport);
        return ret;
      }
      if (ctx->state == USMP_STATE_ESTABLISHED) {
        log_session_id("Session established — id: ", ctx->session_id);
      }
      return USMP_OK;
    }

    case USMP_STATE_ESTABLISHED:
      return usmp_keepalive_tick(ctx);

    case USMP_STATE_ERROR:
    default:
      return USMP_ERR_AUTH_FAILED;
  }
}

usmp_err_t usmp_connect(usmp_t* ctx, usmp_transport_t* transport) {
  usmp_err_t ret = usmp_connect_async(ctx, transport);
  if (ret != USMP_OK) return ret;

  while (ctx->state != USMP_STATE_ESTABLISHED && ctx->state != USMP_STATE_ERROR) {
    usmp_port_wdt_feed();
    ret = usmp_step(ctx);
    if (ret != USMP_OK) {
      if (ctx->tx_mutex) {
        usmp_port_mutex_destroy(ctx->tx_mutex);
        ctx->tx_mutex = NULL;
      }
      if (ctx->rx_mutex) {
        usmp_port_mutex_destroy(ctx->rx_mutex);
        ctx->rx_mutex = NULL;
      }
      return ret;
    }
    if (ctx->state != USMP_STATE_ESTABLISHED) {
      usmp_port_delay_ms(2);
    }
  }

  if (ctx->state == USMP_STATE_ESTABLISHED) {
    return USMP_OK;
  }
  return USMP_ERR_AUTH_FAILED;
}

usmp_err_t usmp_reconnect(usmp_t* ctx) {
  usmp_err_t ret = usmp_reconnect_async(ctx);
  if (ret != USMP_OK) return ret;

  while (ctx->state != USMP_STATE_ESTABLISHED && ctx->state != USMP_STATE_ERROR) {
    usmp_port_wdt_feed();
    ret = usmp_step(ctx);
    if (ret != USMP_OK) return ret;
    if (ctx->state != USMP_STATE_ESTABLISHED) {
      usmp_port_delay_ms(2);
    }
  }

  if (ctx->state == USMP_STATE_ESTABLISHED) {
    return USMP_OK;
  }
  return USMP_ERR_AUTH_FAILED;
}

void usmp_close(usmp_t* ctx) {
  if (!ctx) return;

  if (ctx->hs_ctx) {
    usmp_handshake_abort(&ctx->transport, ctx);
  }
  ctx->state = USMP_STATE_IDLE;

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

const char* usmp_get_version(void) { return "1.3.0"; }

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
