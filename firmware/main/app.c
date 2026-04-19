#include "dxp.h"
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

    dxp_t ctx = {0};
    if (dxp_connect(&ctx, "192.168.137.1", DXP_DEFAULT_PORT) != 0)
    {
        ESP_LOGE(TAG, "DXP connect failed");
        return;
    }

    // Send a test message
    const char *msg = "hello encrypted world";
    if (dxp_send(&ctx, (const uint8_t *)msg, strlen(msg)) == 0)
    {
        ESP_LOGI(TAG, "Message sent");
    }

    // Keep alive loop
    while (dxp_is_connected(&ctx))
    {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (dxp_send(&ctx, (const uint8_t *)"ping", 4) != 0)
        {
            ESP_LOGW(TAG, "Send failed, disconnecting");
            break;
        }
    }

    dxp_close(&ctx);
}