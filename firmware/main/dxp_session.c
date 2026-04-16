#include "dxp_session.h"
#include "dxp_frame.h"
#include "dxp_crypto.h"
#include "dxp_transport.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "DXP_SESSION";

int dxp_send(dxp_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    dxp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.magic = DXP_MAGIC;
    pkt.version = 1;
    pkt.type = DXP_TYPE_DATA;
    pkt.seq = ctx->tx_seq;

    // Build AAD = header without crc and without payload
    // (magic + ver + type + seq + length) = 10 bytes
    uint8_t aad[10];
    aad[0] = pkt.magic & 0xFF;
    aad[1] = (pkt.magic >> 8) & 0xFF;
    aad[2] = pkt.version;
    aad[3] = pkt.type;
    aad[4] = pkt.seq & 0xFF;
    aad[5] = (pkt.seq >> 8) & 0xFF;
    aad[6] = (pkt.seq >> 16) & 0xFF;
    aad[7] = (pkt.seq >> 24) & 0xFF;
    // length will be set after encryption — use 0 for AAD (or pre-compute)
    aad[8] = 0;
    aad[9] = 0;

    size_t out_len = 0;
    if (dxp_gcm_encrypt(ctx->hs.session_key, ctx->tx_seq,
                        ctx->hs.session_id,
                        aad, sizeof(aad),
                        data, len,
                        pkt.payload, &out_len) != 0)
    {
        ESP_LOGE(TAG, "Encryption failed");
        return -1;
    }

    pkt.length = (uint16_t)out_len;

    uint8_t tx_buf[DXP_HEADER_SIZE + DXP_MAX_PAYLOAD];
    uint16_t tx_len = 0;
    dxp_build_packet(&pkt, tx_buf, &tx_len);

    if (dxp_tcp_send(ctx->sock, tx_buf, tx_len) < 0)
    {
        ESP_LOGE(TAG, "Send failed");
        return -1;
    }

    ESP_LOGI(TAG, "Sent encrypted frame seq=%lu len=%d", ctx->tx_seq, len);
    ctx->tx_seq++;
    return 0;
}

int dxp_recv(dxp_ctx_t *ctx, uint8_t *out, uint16_t max_len)
{
    uint8_t rx_buf[DXP_HEADER_SIZE + DXP_MAX_PAYLOAD];
    dxp_packet_t pkt;

    int len = dxp_tcp_recv(ctx->sock, rx_buf, sizeof(rx_buf));
    if (len < 0)
    {
        ESP_LOGE(TAG, "Recv failed");
        return -1;
    }

    if (dxp_parse_packet(rx_buf, len, &pkt) != 0)
    {
        ESP_LOGE(TAG, "Parse failed");
        return -1;
    }

    if (pkt.type != DXP_TYPE_DATA)
    {
        ESP_LOGE(TAG, "Unexpected type 0x%02x", pkt.type);
        return -1;
    }

    // Replay check
    if (pkt.seq != ctx->rx_seq)
    {
        ESP_LOGE(TAG, "Seq mismatch: expected %lu got %lu", ctx->rx_seq, pkt.seq);
        return -1;
    }

    // Rebuild AAD
    uint8_t aad[10];
    aad[0] = pkt.magic & 0xFF;
    aad[1] = (pkt.magic >> 8) & 0xFF;
    aad[2] = pkt.version;
    aad[3] = pkt.type;
    aad[4] = pkt.seq & 0xFF;
    aad[5] = (pkt.seq >> 8) & 0xFF;
    aad[6] = (pkt.seq >> 16) & 0xFF;
    aad[7] = (pkt.seq >> 24) & 0xFF;
    aad[8] = 0;
    aad[9] = 0;

    size_t out_len = 0;
    if (dxp_gcm_decrypt(ctx->hs.session_key, pkt.seq,
                        ctx->hs.session_id,
                        aad, sizeof(aad),
                        pkt.payload, pkt.length,
                        out, &out_len) != 0)
    {
        ESP_LOGE(TAG, "Decryption/auth failed — possible tampering");
        return -1;
    }

    ctx->rx_seq++;
    ESP_LOGI(TAG, "Recv decrypted frame seq=%lu len=%d", pkt.seq, (int)out_len);
    return (int)out_len;
}