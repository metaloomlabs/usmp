// examples/project_tcp/esp32/main/app.c
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usmp.h"
#include "usmp_transport.h"
#include "wifi.h"

static const char* TAG = "APP";

void app_main(void) {
  ESP_LOGI(TAG, "Initializing Wi-Fi");
  if (!wifi_init()) {
    ESP_LOGE(TAG, "Wi-Fi init failed");
    return;
  }

  const char* server_ip = "[IP_ADDRESS]";
  const int port = 9000;

  usmp_t ctx = {0};
  usmp_transport_t transport = {0};
  static usmp_tcp_ctx_t s_tcp_ctx = {0};

  /*
   * Set PSK at runtime — compile-time USMP_PSK is no longer supported.
   * Replace this with a call to your secure storage / NVS provisioning
   * system in production firmware.
   */
  static const uint8_t s_psk[] = "usmp-dev-psk-change-me-before-prod";
  ctx.psk = s_psk;
  ctx.psk_len = sizeof(s_psk) - 1;  // exclude null terminator

  // Enable verbose logging to observe handshake and packet exchanges
  usmp_set_log_level(USMP_LOG_LEVEL_INFO);

  // ── Initial connect with infinite retry loop using zero-heap static transport ──
  int attempt = 0;
  while (true) {
    attempt++;
    if (usmp_transport_tcp_init_static(&transport, &s_tcp_ctx, server_ip, port) == 0) {
      ESP_LOGI(TAG, "TCP connected via static transport (attempt %d)", attempt);
      break;
    }
    ESP_LOGW(TAG, "Connect attempt %d failed, retrying in 2s...", attempt);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }

  int err = usmp_connect(&ctx, &transport);
  if (err != 0) {
    ESP_LOGE(TAG, "USMP connect failed: %s (code %d)", usmp_strerror(err), err);
    return;
  }

  ctx.keepalive_ms = 15000;  // PING every 15s if idle

  const char* msg = "hello from esp32 (zero-heap static transport)";
  err = usmp_send(&ctx, (const uint8_t*)msg, strlen(msg));
  if (err == 0) {
    ESP_LOGI(TAG, "Initial message sent successfully");
  } else {
    ESP_LOGE(TAG, "Initial message send failed: %s (%d)", usmp_strerror(err), err);
  }

  // ── Main loop — keepalive, telemetry & receive ─────────────────────────────
  int counter = 0;
  TickType_t last_send_tick = xTaskGetTickCount();

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(50));

    // Read incoming messages if available
    if (ctx.transport.available && ctx.transport.available(&ctx.transport) > 0) {
      uint8_t rx_buf[USMP_MAX_DATA_LEN * USMP_MAX_FRAMES + 1];
      int n = usmp_recv(&ctx, rx_buf, USMP_MAX_DATA_LEN * USMP_MAX_FRAMES);
      if (n > 0) {
        rx_buf[n] = '\0';
        ESP_LOGI(TAG, "RX Echoed: %s", (char*)rx_buf);
      } else if (n == USMP_ERR_PEER_CLOSED) {
        ESP_LOGW(TAG, "Peer closed gracefully: %s (%d)", usmp_strerror(n), n);
      } else if (n < 0) {
        ESP_LOGE(TAG, "Recv error: %s (%d)", usmp_strerror(n), n);
      }
    }

    // Send telemetry update every 3 seconds
    if ((xTaskGetTickCount() - last_send_tick) >= pdMS_TO_TICKS(3000)) {
      char telemetry[96];
      snprintf(telemetry, sizeof(telemetry), "ESP32 count=%d uptime=%ums heap=%u",
               ++counter, (unsigned int)(xTaskGetTickCount() * portTICK_PERIOD_MS),
               (unsigned int)esp_get_free_heap_size());
      err = usmp_send(&ctx, (const uint8_t*)telemetry, strlen(telemetry));
      if (err == 0) {
        ESP_LOGI(TAG, "TX: %s", telemetry);
      } else {
        ESP_LOGE(TAG, "TX failed: %s (%d)", usmp_strerror(err), err);
      }
      last_send_tick = xTaskGetTickCount();
    }

    if (usmp_keepalive_tick(&ctx) == 0) continue;

    // ── Connection lost — reconnect ───────────────────────────────────────
    ESP_LOGW(TAG, "Connection lost! Reconnecting...");

    int backoff_ms = 2000;
    while ((err = usmp_reconnect(&ctx)) != 0) {
      ESP_LOGW(TAG, "Reconnect failed (%s, code %d), retrying in %dms...",
               usmp_strerror(err), err, backoff_ms);
      vTaskDelay(pdMS_TO_TICKS(backoff_ms));
      if (backoff_ms < 30000) backoff_ms *= 2;  // exponential backoff, cap at 30s
    }

    ESP_LOGI(TAG, "Reconnected successfully");

    // Re-send hello after new session
    if (usmp_send(&ctx, (const uint8_t*)msg, strlen(msg)) == 0) {
      ESP_LOGI(TAG, "Session re-established, message sent");
    }
  }
}
