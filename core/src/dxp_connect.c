#include "dxp.h"
#include "dxp_transport.h"
#include "dxp_handshake.h"
#include "dxp_session.h"
#include "dxp_frame.h"
#include "dxp_port.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "DXP";

int dxp_connect(dxp_t *ctx, dxp_transport_t *transport)
{
    if (!ctx || !transport)
        return -1;

    char _msg[128];
    memset(ctx, 0, sizeof(dxp_t));

    ctx->transport = *transport;

    dxp_t hs = {0};
    hs.psk = ctx->psk;
    hs.psk_len = ctx->psk_len;

    if (dxp_handshake(&ctx->transport, &hs) != 0)
    {
        DXP_LOGE(TAG, "Handshake failed");
        ctx->transport.close(&ctx->transport);
        return -1;
    }

    memcpy(ctx->device_id, hs.device_id, DXP_DEVICE_ID_LEN);
    memcpy(ctx->session_id, hs.session_id, DXP_SESSION_ID_LEN);
    memcpy(ctx->session_key, hs.session_key, DXP_SESSION_KEY_LEN);
    ctx->established = true;
    ctx->tx_seq = 0;
    ctx->rx_seq = 0;
    ctx->last_tx_ms = dxp_port_millis();

    snprintf(_msg, sizeof(_msg), "Session established — id: %02x%02x%02x%02x",
             ctx->session_id[0], ctx->session_id[1],
             ctx->session_id[2], ctx->session_id[3]);
    DXP_LOGI(TAG, _msg);
    return 0;
}

int dxp_reconnect(dxp_t *ctx)
{
    if (!ctx)
        return -1;

    if (!ctx->transport.reconnect)
    {
        DXP_LOGE(TAG, "Transport does not support reconnect");
        return -1;
    }

    ctx->established = false;

    // ── Re-dial transport ─────────────────────────────────────────────────────
    if (ctx->transport.reconnect(&ctx->transport) != 0)
    {
        DXP_LOGE(TAG, "Transport reconnect failed");
        return -1;
    }
    DXP_LOGI(TAG, "Transport reconnected — starting handshake");

    // ── Full new handshake ────────────────────────────────────────────────────
    dxp_t hs = {0};
    hs.psk = ctx->psk;
    hs.psk_len = ctx->psk_len;

    if (dxp_handshake(&ctx->transport, &hs) != 0)
    {
        DXP_LOGE(TAG, "Handshake failed after reconnect");
        ctx->transport.close(&ctx->transport);
        return -1;
    }

    // device_id comes from hardware — unchanged between sessions
    memcpy(ctx->session_id, hs.session_id, DXP_SESSION_ID_LEN);
    memcpy(ctx->session_key, hs.session_key, DXP_SESSION_KEY_LEN);
    ctx->established = true;
    ctx->tx_seq = 0;
    ctx->rx_seq = 0;
    ctx->last_tx_ms = dxp_port_millis();

    char _msg[64];
    snprintf(_msg, sizeof(_msg), "Reconnected — new session: %02x%02x%02x%02x",
             ctx->session_id[0], ctx->session_id[1],
             ctx->session_id[2], ctx->session_id[3]);
    DXP_LOGI(TAG, _msg);
    return 0;
}

void dxp_close(dxp_t *ctx)
{
    if (!ctx)
        return;
    if (ctx->transport.close)
        ctx->transport.close(&ctx->transport);
    ctx->established = false;
    DXP_LOGI(TAG, "Session closed");
}