#include "usmp.h"
#include "usmp_transport.h"
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
    const int port = USMP_DEFAULT_PORT;

    usmp_t ctx = {0};
    usmp_transport_t transport = {0};

    /*
     * Set PSK at runtime — compile-time USMP_PSK is no longer supported.
     * Replace this with a call to your secure storage / NVS provisioning
     * system in production firmware.
     */
    static const uint8_t s_psk[] = "usmp-dev-psk-change-me-before-prod";
    ctx.psk     = s_psk;
    ctx.psk_len = sizeof(s_psk) - 1; // exclude null terminator

    // ── Initial connect with retries ──────────────────────────────────────────
    bool connected = false;
    for (int attempt = 1; attempt <= USMP_CONNECT_RETRIES; ++attempt)
    {
        if (usmp_transport_tcp_init(&transport, server_ip, port) == 0)
        {
            ESP_LOGI(TAG, "TCP connected (attempt %d)", attempt);
            connected = true;
            break;
        }
        ESP_LOGW(TAG, "Attempt %d failed, retrying...", attempt);
        vTaskDelay(pdMS_TO_TICKS(USMP_CONNECT_RETRY_MS));
    }

    if (!connected)
    {
        ESP_LOGE(TAG, "Unable to connect to %s:%d", server_ip, port);
        return;
    }

    if (usmp_connect(&ctx, &transport) != 0)
    {
        ESP_LOGE(TAG, "USMP connect failed");
        return;
    }

    ctx.keepalive_ms = 15000; // PING every 15s if idle

    const char *msg = "hello encrypted world";
    if (usmp_send(&ctx, (const uint8_t *)msg, strlen(msg)) == 0)
        ESP_LOGI(TAG, "Message sent");

    // ── Main loop — keepalive + reconnect ─────────────────────────────────────
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (usmp_keepalive_tick(&ctx) == 0)
            continue;

        // ── Connection lost — reconnect ───────────────────────────────────────
        ESP_LOGW(TAG, "Connection lost, reconnecting...");

        int backoff_ms = 2000;
        while (usmp_reconnect(&ctx) != 0)
        {
            ESP_LOGW(TAG, "Reconnect failed, retrying in %dms...", backoff_ms);
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            if (backoff_ms < 30000)
                backoff_ms *= 2; // exponential backoff, cap at 30s
        }

        ESP_LOGI(TAG, "Reconnected");

        // Re-send hello after new session
        if (usmp_send(&ctx, (const uint8_t *)msg, strlen(msg)) == 0)
            ESP_LOGI(TAG, "Message sent");
    }
}