#include "dxp.h"
#include "dxp_session.h"
#include "dxp_frame.h"
#include "dxp_crypto.h"
#include "dxp_port.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "DXP_SESSION";

// ── Helpers ───────────────────────────────────────────────────────────────────

static void build_aad(
    uint16_t magic, uint8_t version, uint8_t type,
    uint32_t seq, uint16_t length,
    uint8_t *aad)
{
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

// ── Send an encrypted control frame (PING, BYE) with empty plaintext ─────────
static int send_control(dxp_t *ctx, uint8_t type)
{
    dxp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.magic = DXP_MAGIC;
    pkt.version = 1;
    pkt.type = type;
    pkt.seq = ctx->tx_seq;

    uint16_t enc_length = DXP_GCM_TAG_LEN; // empty plaintext, tag only
    uint8_t aad[10];
    build_aad(pkt.magic, pkt.version, pkt.type, pkt.seq, enc_length, aad);

    static const uint8_t empty[1] = {0};
    size_t out_len = 0;
    if (dxp_gcm_encrypt(ctx->session_key, ctx->tx_seq,
                        ctx->session_id,
                        aad, sizeof(aad),
                        empty, 0,
                        pkt.payload, &out_len) != 0)
        return -1;

    pkt.length = (uint16_t)out_len;

    uint8_t tx_buf[DXP_HEADER_SIZE + DXP_MAX_PAYLOAD];
    uint16_t tx_len = 0;
    dxp_build_packet(&pkt, tx_buf, &tx_len);

    if (ctx->transport.send(&ctx->transport, tx_buf, tx_len) < 0)
        return -1;

    ctx->tx_seq++;
    ctx->last_tx_ms = dxp_port_millis();
    return 0;
}

// ── Public API ────────────────────────────────────────────────────────────────

int dxp_send(dxp_t *ctx, const uint8_t *data, uint16_t len)
{
    if (!ctx || !ctx->established)
        return -1;

    char _msg[64];
    dxp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.magic = DXP_MAGIC;
    pkt.version = 1;
    pkt.type = DXP_TYPE_DATA;
    pkt.seq = ctx->tx_seq;

    uint16_t enc_length = len + DXP_GCM_TAG_LEN;
    uint8_t aad[10];
    build_aad(pkt.magic, pkt.version, pkt.type, pkt.seq, enc_length, aad);

    size_t out_len = 0;
    if (dxp_gcm_encrypt(ctx->session_key, ctx->tx_seq,
                        ctx->session_id,
                        aad, sizeof(aad),
                        data, len,
                        pkt.payload, &out_len) != 0)
    {
        DXP_LOGE(TAG, "Encryption failed");
        return -1;
    }

    pkt.length = (uint16_t)out_len;

    uint8_t tx_buf[DXP_HEADER_SIZE + DXP_MAX_PAYLOAD];
    uint16_t tx_len = 0;
    dxp_build_packet(&pkt, tx_buf, &tx_len);

    if (ctx->transport.send(&ctx->transport, tx_buf, tx_len) < 0)
    {
        DXP_LOGE(TAG, "Send failed");
        return -1;
    }

    snprintf(_msg, sizeof(_msg), "TX seq=%lu len=%u",
             (unsigned long)ctx->tx_seq, len);
    DXP_LOGI(TAG, _msg);

    ctx->tx_seq++;
    ctx->last_tx_ms = dxp_port_millis();
    return 0;
}

int dxp_recv(dxp_t *ctx, uint8_t *out, uint16_t max_len)
{
    if (!ctx || !ctx->established)
        return -1;

    char _msg[64];
    uint8_t rx_buf[DXP_HEADER_SIZE + DXP_MAX_PAYLOAD];
    dxp_packet_t pkt;

    int len = ctx->transport.recv(&ctx->transport, rx_buf, sizeof(rx_buf));
    if (len < 0)
    {
        DXP_LOGE(TAG, "Recv failed");
        ctx->established = false; // mark dead so caller knows to reconnect
        return -1;
    }

    if (dxp_parse_packet(rx_buf, len, &pkt) != 0)
    {
        DXP_LOGE(TAG, "Parse failed");
        return -1;
    }

    // ── Transparent PONG handling ─────────────────────────────────────────────
    // Server responds to our PING with PONG — consume it and wait for real data
    if (pkt.type == DXP_TYPE_PONG)
    {
        if (pkt.seq != ctx->rx_seq)
        {
            snprintf(_msg, sizeof(_msg), "PONG seq mismatch: expected %lu got %lu",
                     (unsigned long)ctx->rx_seq, (unsigned long)pkt.seq);
            DXP_LOGW(TAG, _msg);
        }
        ctx->rx_seq++;
        DXP_LOGI(TAG, "PONG received");
        return dxp_recv(ctx, out, max_len); // tail-recurse for next frame
    }

    if (pkt.type != DXP_TYPE_DATA)
    {
        snprintf(_msg, sizeof(_msg), "Unexpected type 0x%02x", pkt.type);
        DXP_LOGE(TAG, _msg);
        return -1;
    }

    if (pkt.seq != ctx->rx_seq)
    {
        snprintf(_msg, sizeof(_msg), "Seq mismatch: expected %lu got %lu",
                 (unsigned long)ctx->rx_seq, (unsigned long)pkt.seq);
        DXP_LOGE(TAG, _msg);
        return -1;
    }

    uint8_t aad[10];
    build_aad(pkt.magic, pkt.version, pkt.type, pkt.seq, pkt.length, aad);

    size_t out_len = 0;
    if (dxp_gcm_decrypt(ctx->session_key, pkt.seq,
                        ctx->session_id,
                        aad, sizeof(aad),
                        pkt.payload, pkt.length,
                        out, &out_len) != 0)
    {
        DXP_LOGE(TAG, "Decryption failed");
        return -1;
    }

    ctx->rx_seq++;
    snprintf(_msg, sizeof(_msg), "RX seq=%lu len=%d",
             (unsigned long)pkt.seq, (int)out_len);
    DXP_LOGI(TAG, _msg);
    return (int)out_len;
}

int dxp_ping(dxp_t *ctx)
{
    if (!ctx || !ctx->established)
        return -1;

    if (send_control(ctx, DXP_TYPE_PING) != 0)
    {
        DXP_LOGE(TAG, "PING send failed");
        ctx->established = false;
        return -1;
    }

    DXP_LOGI(TAG, "PING sent");
    return 0;
}

int dxp_keepalive_tick(dxp_t *ctx)
{
    if (!ctx || !ctx->established)
        return -1;

    if (ctx->keepalive_ms == 0)
        return 0; // disabled

    uint32_t now = dxp_port_millis();
    if ((now - ctx->last_tx_ms) >= ctx->keepalive_ms)
        return dxp_ping(ctx);

    return 0;
}