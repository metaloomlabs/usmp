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

int usmp_connect(usmp_t* ctx, usmp_transport_t* transport) {
  if (!ctx || !transport) return -1;

  /* Validate required transport function pointers before use */
  if (!transport->send || !transport->recv) {
    USMP_LOGE(TAG, "Transport missing send or recv — cannot connect");
    return -1;
  }

  const uint8_t* psk = ctx->psk;
  size_t psk_len = ctx->psk_len;
  uint32_t keepalive_ms = ctx->keepalive_ms;

  memset(ctx, 0, sizeof(usmp_t));

  ctx->transport = *transport;
  ctx->psk = psk;
  ctx->psk_len = psk_len;
  ctx->keepalive_ms = keepalive_ms;

  usmp_t hs = {0};
  hs.psk = ctx->psk;
  hs.psk_len = ctx->psk_len;

  int ret = 0;
  if (usmp_handshake(&ctx->transport, &hs) != 0) {
    USMP_LOGE(TAG, "Handshake failed");
    if (ctx->transport.close) ctx->transport.close(&ctx->transport);
    ret = -1;
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
  ret = 0;

cleanup:
  mbedtls_platform_zeroize(&hs, sizeof(hs));
  return ret;
}

int usmp_reconnect(usmp_t* ctx) {
  if (!ctx) return -1;

  /* Zeroise the old session key immediately on entering reconnect */
  mbedtls_platform_zeroize(ctx->tx_key, sizeof(ctx->tx_key));
  mbedtls_platform_zeroize(ctx->rx_key, sizeof(ctx->rx_key));

  if (!ctx->transport.reconnect) {
    USMP_LOGE(TAG, "Transport does not support reconnect");
    return -1;
  }

  ctx->established = false;

  //  Re-dial transport ─────────────────────────────────────────────────────
  if (ctx->transport.reconnect(&ctx->transport) != 0) {
    USMP_LOGE(TAG, "Transport reconnect failed");
    return -1;
  }
  USMP_LOGI(TAG, "Transport reconnected — starting handshake");

  // Full new handshake ────────────────────────────────────────────────────
  usmp_t hs = {0};
  hs.psk = ctx->psk;
  hs.psk_len = ctx->psk_len;

  int ret = 0;
  if (usmp_handshake(&ctx->transport, &hs) != 0) {
    USMP_LOGE(TAG, "Handshake failed after reconnect");
    if (ctx->transport.close) ctx->transport.close(&ctx->transport);
    ret = -1;
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
  ret = 0;

cleanup:
  mbedtls_platform_zeroize(&hs, sizeof(hs));
  return ret;
}

void usmp_close(usmp_t* ctx) {
  if (!ctx) return;
  /*
   * Courtesy BYE so the peer can release the session immediately instead of
   * holding it until its inactivity watchdog fires. Best-effort: the session is
   * over regardless, so an undelivered BYE is ignored. It must go out before we
   * tear down the transport or zeroise the tx_key it is encrypted under.
   */
  if (ctx->established) usmp_send_bye(ctx);
  ctx->established = false;
  if (ctx->transport.close) ctx->transport.close(&ctx->transport);
  mbedtls_platform_zeroize(ctx->tx_key, sizeof(ctx->tx_key));
  mbedtls_platform_zeroize(ctx->rx_key, sizeof(ctx->rx_key));
  USMP_LOGI(TAG, "Session closed");
}

const char* usmp_get_version(void) { return "1.2.0"; }

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
