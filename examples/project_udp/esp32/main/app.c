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

  // Enable verbose logging to observe handshake and packet exchanges
  usmp_set_log_level(USMP_LOG_LEVEL_INFO);

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
    ESP_LOGE(TAG, "USMP connect failed over UDP: %s (code %d)", usmp_strerror(ctx.last_err), ctx.last_err);
    return;
  }

  ctx.keepalive_ms = 15000;  // PING every 15s if idle

  const char* msg = "hello from esp32 over UDP";
  if (usmp_send(&ctx, (const uint8_t*)msg, strlen(msg)) == 0) ESP_LOGI(TAG, "Message sent");

  // ── Main loop — periodic telemetry, receive & keepalive ────────────────────
  int counter = 0;
  TickType_t last_send_tick = xTaskGetTickCount();

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(50));

    // Send telemetry periodically every 3 seconds
    if (xTaskGetTickCount() - last_send_tick >= pdMS_TO_TICKS(3000)) {
      last_send_tick = xTaskGetTickCount();
      counter++;
      char telemetry[80];
      uint32_t heap = esp_get_free_heap_size();
      snprintf(telemetry, sizeof(telemetry), "ESP32 UDP count=%d uptime=%lums heap=%lu",
               counter, (unsigned long)usmp_port_millis(), (unsigned long)heap);
      ESP_LOGI(TAG, "TX: %s", telemetry);
      if (usmp_send(&ctx, (const uint8_t*)telemetry, strlen(telemetry)) != 0) {
        ESP_LOGE(TAG, "Failed to send UDP telemetry: %s", usmp_strerror(ctx.last_err));
      }
    }

    // Read incoming messages if available
    if (ctx.transport.available && ctx.transport.available(&ctx.transport) > 0) {
      uint8_t rx_buf[USMP_MAX_DATA_LEN * USMP_MAX_FRAMES + 1];
      int n = usmp_recv(&ctx, rx_buf, USMP_MAX_DATA_LEN * USMP_MAX_FRAMES);
      if (n > 0) {
        rx_buf[n] = '\0';
        ESP_LOGI(TAG, "RX Echoed: %s", (char*)rx_buf);
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
