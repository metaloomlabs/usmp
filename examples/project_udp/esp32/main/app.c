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

  static const uint8_t s_psk[] = "usmp-dev-psk-change-me-before-prod";
  ctx.psk = s_psk;
  ctx.psk_len = sizeof(s_psk) - 1;  // exclude null terminator

  // ── Initial connect with retries ──────────────────────────────────────────
  bool connected = false;
  for (int attempt = 1; attempt <= USMP_CONNECT_RETRIES; ++attempt) {
    if (usmp_transport_udp_init(&transport, server_ip, port) == 0) {
      ESP_LOGI(TAG, "UDP transport initialized (attempt %d)", attempt);
      connected = true;
      break;
    }
    ESP_LOGW(TAG, "Attempt %d failed, retrying...", attempt);
    vTaskDelay(pdMS_TO_TICKS(USMP_CONNECT_RETRY_MS));
  }

  if (!connected) {
    ESP_LOGE(TAG, "Unable to initialize UDP to %s:%d", server_ip, port);
    return;
  }

  if (usmp_connect(&ctx, &transport) != 0) {
    ESP_LOGE(TAG, "USMP connect failed over UDP");
    return;
  }

  ctx.keepalive_ms = 15000;  // PING every 15s if idle

  const char* msg = "hello from esp32 over UDP";
  if (usmp_send(&ctx, (const uint8_t*)msg, strlen(msg)) == 0) ESP_LOGI(TAG, "Message sent");

  // ── Main loop — keepalive + reconnect ─────────────────────────────────────
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(100));

    // Read incoming messages if available
    if (ctx.transport.available && ctx.transport.available(&ctx.transport) > 0) {
      uint8_t rx_buf[USMP_MAX_DATA_LEN * USMP_MAX_FRAMES + 1];
      int n = usmp_recv(&ctx, rx_buf, USMP_MAX_DATA_LEN * USMP_MAX_FRAMES);
      if (n > 0) {
        rx_buf[n] = '\0';
        ESP_LOGI(TAG, "RX-UDP: %s", (char*)rx_buf);
      }
    }

    if (usmp_keepalive_tick(&ctx) == 0) continue;

    // ── Connection lost — reconnect ───────────────────────────────────────
    ESP_LOGW(TAG, "Connection lost, reconnecting...");

    int backoff_ms = 2000;
    while (usmp_reconnect(&ctx) != 0) {
      ESP_LOGW(TAG, "Reconnect failed, retrying in %dms...", backoff_ms);
      vTaskDelay(pdMS_TO_TICKS(backoff_ms));
      if (backoff_ms < 30000) backoff_ms *= 2;
    }

    ESP_LOGI(TAG, "Reconnected");

    // Re-send hello after new session
    if (usmp_send(&ctx, (const uint8_t*)msg, strlen(msg)) == 0) ESP_LOGI(TAG, "Message sent");
  }
}
