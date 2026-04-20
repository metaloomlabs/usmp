#include "dxp.h"
#include "dxp_session.h"
#include "dxp_frame.h"
#include "dxp_crypto.h"
#include "dxp_port.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "DXP_SESSION";

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
        return -1;
    }

    if (dxp_parse_packet(rx_buf, len, &pkt) != 0)
    {
        DXP_LOGE(TAG, "Parse failed");
        return -1;
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