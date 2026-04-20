#include "dxp.h"
#include "dxp_transport.h"
#include "dxp_handshake.h"
#include "dxp_session.h"
#include "dxp_frame.h"
#include "dxp_port.h"
#include <unistd.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "DXP";

int dxp_connect(dxp_t *ctx, const char *server_ip, int port)
{
    if (!ctx)
        return -1;

    char _msg[128];
    memset(ctx, 0, sizeof(dxp_t));
    ctx->sock = -1;

    // ── TCP connect with retries ──────────────────────────────────────────────
    for (int attempt = 1; attempt <= DXP_CONNECT_RETRIES; ++attempt)
    {
        ctx->sock = dxp_tcp_connect(server_ip, port);
        if (ctx->sock >= 0)
        {
            snprintf(_msg, sizeof(_msg), "TCP connected (attempt %d)", attempt);
            DXP_LOGI(TAG, _msg);
            break;
        }
        snprintf(_msg, sizeof(_msg),
                 "Connection attempt %d failed, retrying in %dms...",
                 attempt, DXP_CONNECT_RETRY_MS);
        DXP_LOGW(TAG, _msg);
        dxp_port_delay_ms(DXP_CONNECT_RETRY_MS);
    }

    if (ctx->sock < 0)
    {
        snprintf(_msg, sizeof(_msg), "Unable to connect to %s:%d", server_ip, port);
        DXP_LOGE(TAG, _msg);
        return -1;
    }

    // ── DXP handshake ─────────────────────────────────────────────────────────
    dxp_session_t hs = {0};
    if (dxp_handshake(ctx->sock, &hs) != 0)
    {
        DXP_LOGE(TAG, "Handshake failed");
        close(ctx->sock);
        ctx->sock = -1;
        return -1;
    }

    // ── Copy session info into public context ─────────────────────────────────
    memcpy(ctx->device_id, hs.device_id, DXP_DEVICE_ID_LEN);
    memcpy(ctx->session_id, hs.session_id, DXP_SESSION_ID_LEN);
    memcpy(ctx->session_key, hs.session_key, DXP_SESSION_KEY_LEN);
    ctx->established = true;
    ctx->tx_seq = 0;
    ctx->rx_seq = 0;

    snprintf(_msg, sizeof(_msg), "Session established — id: %02x%02x%02x%02x",
             ctx->session_id[0], ctx->session_id[1],
             ctx->session_id[2], ctx->session_id[3]);
    DXP_LOGI(TAG, _msg);

    return 0;
}

void dxp_close(dxp_t *ctx)
{
    if (!ctx)
        return;
    if (ctx->sock >= 0)
    {
        close(ctx->sock);
        ctx->sock = -1;
    }
    ctx->established = false;
    DXP_LOGI(TAG, "Session closed");
}