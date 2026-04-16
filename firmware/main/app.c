#include "dxp_frame.h"
#include "dxp_transport.h"
#include "dxp_handshake.h"
#include "dxp_session.h"
#include "wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <unistd.h>
#include <string.h>

static const char *TAG = "DXP_APP";

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing Wi-Fi");
    if (!wifi_init())
    {
        ESP_LOGE(TAG, "Wi-Fi init failed");
        return;
    }

    const char *server_ip = "192.168.137.1";
    const int server_port = 9000;
    int sock = -1;

    for (int attempt = 1; attempt <= 10; ++attempt)
    {
        sock = dxp_tcp_connect(server_ip, server_port);
        if (sock >= 0)
        {
            ESP_LOGI(TAG, "TCP connected (attempt %d)", attempt);
            break;
        }
        ESP_LOGW(TAG, "Connection attempt %d failed, retrying in 2s...", attempt);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    if (sock < 0)
    {
        ESP_LOGE(TAG, "Unable to connect to %s:%d", server_ip, server_port);
        return;
    }

    dxp_session_t hs = {0};
    if (dxp_handshake(sock, &hs) != 0)
    {
        ESP_LOGE(TAG, "Handshake failed");
        close(sock);
        return;
    }

    ESP_LOGI(TAG, "DXP session established");

    // Initialize session context
    dxp_ctx_t ctx = {
        .hs = hs,
        .tx_seq = 0,
        .rx_seq = 0,
        .sock = sock,
    };

    // Send test encrypted frame
    const char *msg = "hello encrypted world";
    if (dxp_send(&ctx, (const uint8_t *)msg, strlen(msg)) == 0)
    {
        ESP_LOGI(TAG, "Encrypted frame sent");
    }

    close(sock);
}