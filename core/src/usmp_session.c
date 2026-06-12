#include "usmp_session.h"
#include "usmp.h"
#include "usmp_crypto.h"
#include "usmp_frame.h"
#include "usmp_port.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "USMP_SESSION";

// Helpers ───────────────────────────────────────────────────────────────────

static void build_aad(uint16_t magic, uint8_t version, uint8_t type,
                      uint32_t seq, uint16_t length, uint8_t *aad) {
  aad[0] = magic & 0xFF;
  aad[1] = (magic >> 8) & 0xFF;
  aad[2] = version;
  aad[3] = type;
  aad[4] = seq & 0xFF;
  aad[5] = (seq >> 8) & 0xFF;
  aad[6] = (seq >> 16) & 0xFF;
  aad[7] = (seq >> 24) & 0xFF;
  aad[8] = length & 0xFF;
  aad[9] = (length >> 8) & 0xFF;
}

// Send an encrypted control frame (PING, BYE) with empty plaintext ─────────
static int send_control(usmp_t *ctx, uint8_t type) {
  usmp_packet_t pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.magic = USMP_MAGIC;
  pkt.version = 1;
  pkt.type = type;
  pkt.seq = ctx->tx_seq;

  uint16_t enc_length = USMP_GCM_TAG_LEN; // empty plaintext, tag only
  uint8_t aad[10];
  build_aad(pkt.magic, pkt.version, pkt.type, pkt.seq, enc_length, aad);

  static const uint8_t empty[1] = {0};
  size_t out_len = 0;
  if (usmp_gcm_encrypt(ctx->session_key, ctx->tx_seq, ctx->session_id, aad,
                       sizeof(aad), empty, 0, pkt.payload, &out_len) != 0)
    return -1;

  pkt.length = (uint16_t)out_len;

  uint8_t tx_buf[USMP_HEADER_SIZE + USMP_MAX_PAYLOAD];
  uint16_t tx_len = 0;
  usmp_build_packet(&pkt, tx_buf, &tx_len);

  if (ctx->transport.send(&ctx->transport, tx_buf, tx_len) < 0)
    return -1;

  ctx->tx_seq++;
  ctx->last_tx_ms = usmp_port_millis();
  return 0;
}

// Public API ────────────────────────────────────────────────────────────────

int usmp_send(usmp_t *ctx, const uint8_t *data, uint16_t len) {
  if (!ctx || !ctx->established)
    return -1;

  char _msg[64];
  usmp_packet_t pkt;
  memset(&pkt, 0, sizeof(pkt));

  pkt.magic = USMP_MAGIC;
  pkt.version = 1;
  pkt.type = USMP_TYPE_DATA;
  pkt.seq = ctx->tx_seq;

  uint16_t enc_length = len + USMP_GCM_TAG_LEN;
  uint8_t aad[10];
  build_aad(pkt.magic, pkt.version, pkt.type, pkt.seq, enc_length, aad);

  size_t out_len = 0;
  if (usmp_gcm_encrypt(ctx->session_key, ctx->tx_seq, ctx->session_id, aad,
                       sizeof(aad), data, len, pkt.payload, &out_len) != 0) {
    USMP_LOGE(TAG, "Encryption failed");
    return -1;
  }

  pkt.length = (uint16_t)out_len;

  uint8_t tx_buf[USMP_HEADER_SIZE + USMP_MAX_PAYLOAD];
  uint16_t tx_len = 0;
  usmp_build_packet(&pkt, tx_buf, &tx_len);

  if (ctx->transport.send(&ctx->transport, tx_buf, tx_len) < 0) {
    USMP_LOGE(TAG, "Send failed");
    return -1;
  }

  snprintf(_msg, sizeof(_msg), "TX seq=%lu len=%u", (unsigned long)ctx->tx_seq,
           len);
  USMP_LOGI(TAG, _msg);

  ctx->tx_seq++;
  ctx->last_tx_ms = usmp_port_millis();
  return 0;
}

int usmp_recv(usmp_t *ctx, uint8_t *out, uint16_t max_len) {
  if (!ctx || !ctx->established)
    return -1;

  char _msg[64];
  uint8_t rx_buf[USMP_HEADER_SIZE + USMP_MAX_PAYLOAD];
  usmp_packet_t pkt;

  int len = ctx->transport.recv(&ctx->transport, rx_buf, sizeof(rx_buf));
  if (len < 0) {
    USMP_LOGE(TAG, "Recv failed");
    ctx->established = false; // mark dead so caller knows to reconnect
    return -1;
  }

  if (usmp_parse_packet(rx_buf, len, &pkt) != 0) {
    USMP_LOGE(TAG, "Parse failed");
    return -1;
  }

  if (pkt.seq != ctx->rx_seq) {
    snprintf(_msg, sizeof(_msg), "Seq mismatch: expected %lu got %lu",
             (unsigned long)ctx->rx_seq, (unsigned long)pkt.seq);
    USMP_LOGE(TAG, _msg);
    return -1;
  }

  uint8_t dummy_out[1];
  uint8_t *dec_dest = out;
  uint16_t dec_max = max_len;

  if (pkt.type == USMP_TYPE_PONG || pkt.type == USMP_TYPE_PING ||
      pkt.type == USMP_TYPE_BYE) {
    dec_dest = dummy_out;
    dec_max = sizeof(dummy_out);
  } else if (pkt.type != USMP_TYPE_DATA) {
    snprintf(_msg, sizeof(_msg), "Unexpected type 0x%02x", pkt.type);
    USMP_LOGE(TAG, _msg);
    return -1;
  }

  uint8_t aad[10];
  build_aad(pkt.magic, pkt.version, pkt.type, pkt.seq, pkt.length, aad);

  size_t out_len = 0;
  if (usmp_gcm_decrypt(ctx->session_key, pkt.seq, ctx->session_id, aad,
                       sizeof(aad), pkt.payload, pkt.length, dec_dest,
                       &out_len) != 0) {
    USMP_LOGE(TAG, "Decryption failed");
    return -1;
  }

  ctx->rx_seq++;

  if (pkt.type == USMP_TYPE_PONG) {
    USMP_LOGI(TAG, "PONG received");
    return usmp_recv(ctx, out, max_len); // tail-recurse for next frame
  } else if (pkt.type == USMP_TYPE_PING) {
    USMP_LOGI(TAG, "PING received — responding with PONG");
    if (send_control(ctx, USMP_TYPE_PONG) != 0) {
      USMP_LOGE(TAG, "Failed to send PONG");
      ctx->established = false;
      return -1;
    }
    return usmp_recv(ctx, out, max_len); // tail-recurse for next frame
  } else if (pkt.type == USMP_TYPE_BYE) {
    USMP_LOGI(TAG, "BYE received — session closed by peer");
    ctx->established = false;
    return -1;
  }

  snprintf(_msg, sizeof(_msg), "RX seq=%lu len=%d", (unsigned long)pkt.seq,
           (int)out_len);
  USMP_LOGI(TAG, _msg);
  return (int)out_len;
}

int usmp_ping(usmp_t *ctx) {
  if (!ctx || !ctx->established)
    return -1;

  if (send_control(ctx, USMP_TYPE_PING) != 0) {
    USMP_LOGE(TAG, "PING send failed");
    ctx->established = false;
    return -1;
  }

  USMP_LOGI(TAG, "PING sent");
  return 0;
}

int usmp_keepalive_tick(usmp_t *ctx) {
  if (!ctx || !ctx->established)
    return -1;

  if (ctx->keepalive_ms == 0)
    return 0; // disabled

  uint32_t now = usmp_port_millis();
  if ((now - ctx->last_tx_ms) >= ctx->keepalive_ms)
    return usmp_ping(ctx);

  return 0;
}
