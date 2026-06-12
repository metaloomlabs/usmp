#include "usmp.h"
#include "usmp_frame.h"
#include "usmp_handshake.h"
#include "usmp_port.h"
#include "usmp_session.h"
#include "usmp_transport.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "USMP";

int usmp_connect(usmp_t *ctx, usmp_transport_t *transport) {
  if (!ctx || !transport)
    return -1;

  char _msg[128];
  const uint8_t *psk = ctx->psk;
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

  if (usmp_handshake(&ctx->transport, &hs) != 0) {
    USMP_LOGE(TAG, "Handshake failed");
    ctx->transport.close(&ctx->transport);
    return -1;
  }

  memcpy(ctx->device_id, hs.device_id, USMP_DEVICE_ID_LEN);
  memcpy(ctx->session_id, hs.session_id, USMP_SESSION_ID_LEN);
  memcpy(ctx->session_key, hs.session_key, USMP_SESSION_KEY_LEN);
  ctx->established = true;
  ctx->tx_seq = 0;
  ctx->rx_seq = 0;
  ctx->last_tx_ms = usmp_port_millis();

  snprintf(_msg, sizeof(_msg), "Session established — id: %02x%02x%02x%02x",
           ctx->session_id[0], ctx->session_id[1], ctx->session_id[2],
           ctx->session_id[3]);
  USMP_LOGI(TAG, _msg);
  return 0;
}

int usmp_reconnect(usmp_t *ctx) {
  if (!ctx)
    return -1;

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

  if (usmp_handshake(&ctx->transport, &hs) != 0) {
    USMP_LOGE(TAG, "Handshake failed after reconnect");
    ctx->transport.close(&ctx->transport);
    return -1;
  }

  // device_id comes from hardware — unchanged between sessions
  memcpy(ctx->session_id, hs.session_id, USMP_SESSION_ID_LEN);
  memcpy(ctx->session_key, hs.session_key, USMP_SESSION_KEY_LEN);
  ctx->established = true;
  ctx->tx_seq = 0;
  ctx->rx_seq = 0;
  ctx->last_tx_ms = usmp_port_millis();

  char _msg[64];
  snprintf(_msg, sizeof(_msg), "Reconnected — new session: %02x%02x%02x%02x",
           ctx->session_id[0], ctx->session_id[1], ctx->session_id[2],
           ctx->session_id[3]);
  USMP_LOGI(TAG, _msg);
  return 0;
}

void usmp_close(usmp_t *ctx) {
  if (!ctx)
    return;
  if (ctx->transport.close)
    ctx->transport.close(&ctx->transport);
  ctx->established = false;
  USMP_LOGI(TAG, "Session closed");
}
