#include "dxp.h"
#include "dxp_transport.h"
#include "wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "APP";

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing Wi-Fi");
    if (!wifi_init())
    {
        ESP_LOGE(TAG, "Wi-Fi init failed");
        return;
    }

    const char *server_ip = "192.168.137.1";
    const int port = DXP_DEFAULT_PORT;

    dxp_t ctx = {0};
    dxp_transport_t transport = {0};

    // ── Connect transport with retries ────────────────────────────────────────
    bool connected = false;
    for (int attempt = 1; attempt <= DXP_CONNECT_RETRIES; ++attempt)
    {
        if (dxp_transport_tcp_init(&transport, server_ip, port) == 0)
        {
            ESP_LOGI(TAG, "TCP connected (attempt %d)", attempt);
            connected = true;
            break;
        }
        ESP_LOGW(TAG, "Attempt %d failed, retrying...", attempt);
        vTaskDelay(pdMS_TO_TICKS(DXP_CONNECT_RETRY_MS));
    }

    if (!connected)
    {
        ESP_LOGE(TAG, "Unable to connect to %s:%d", server_ip, port);
        return;
    }

    // ── DXP handshake + session ───────────────────────────────────────────────
    if (dxp_connect(&ctx, &transport) != 0)
    {
        ESP_LOGE(TAG, "DXP connect failed");
        return;
    }

    // ── Send test message ─────────────────────────────────────────────────────
    const char *msg = "hello encrypted world";
    if (dxp_send(&ctx, (const uint8_t *)msg, strlen(msg)) == 0)
        ESP_LOGI(TAG, "Message sent");

    // ── Keepalive loop ────────────────────────────────────────────────────────
    while (dxp_is_connected(&ctx))
    {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (dxp_send(&ctx, (const uint8_t *)"ping", 4) != 0)
        {
            ESP_LOGW(TAG, "Send failed");
            break;
        }
    }

    dxp_close(&ctx);
}