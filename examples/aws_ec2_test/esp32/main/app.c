// examples/aws_ec2_test/esp32/main/app.c
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usmp.h"
#include "usmp_transport.h"
#include "wifi.h"

static const char* TAG = "APP";

#define PSK "usmp-dev-psk-change-me-before-prod"
#define EC2_PUBLIC_IP "YOUR_EC2_PUBLIC_IP"
#define SERVER_PORT 9000

void app_main(void) {
  ESP_LOGI(TAG, "Initializing Wi-Fi STA mode...");
  if (!wifi_init()) {
    ESP_LOGE(TAG, "Wi-Fi initialization failed!");
    return;
  }

  usmp_t ctx = {0};
  usmp_transport_t transport = {0};

  static const uint8_t s_psk[] = PSK;
  ctx.psk = s_psk;
  ctx.psk_len = sizeof(s_psk) - 1;  // exclude null terminator

  // Enable verbose log level for handshake details
  usmp_set_log_level(USMP_LOG_LEVEL_INFO);

  ESP_LOGI(TAG, "Connecting to USMP EC2 Server at %s:%d", EC2_PUBLIC_IP, SERVER_PORT);

  // ── Initial connect with retries ──────────────────────────────────────────
  bool connected = false;
  for (int attempt = 1; attempt <= USMP_CONNECT_RETRIES; ++attempt) {
    if (usmp_transport_tcp_init(&transport, EC2_PUBLIC_IP, SERVER_PORT) == 0) {
      ESP_LOGI(TAG, "TCP connected to EC2 (attempt %d)", attempt);
      connected = true;
      break;
    }
    ESP_LOGW(TAG, "Attempt %d failed, retrying in %d ms...", attempt, USMP_CONNECT_RETRY_MS);
    vTaskDelay(pdMS_TO_TICKS(USMP_CONNECT_RETRY_MS));
  }

  if (!connected) {
    ESP_LOGE(TAG, "Failed to establish TCP connection to EC2 at %s:%d", EC2_PUBLIC_IP, SERVER_PORT);
    ESP_LOGE(TAG, "Please check Security Groups and verify server_ec2.py is running.");
    return;
  }

  // Perform secure mutually-authenticated handshake
  if (usmp_connect(&ctx, &transport) != 0) {
    ESP_LOGE(TAG, "USMP secure handshake with EC2 failed! Check PSK.");
    return;
  }

  ESP_LOGI(TAG, "Secure USMP session established successfully!");
  ctx.keepalive_ms = 15000;  // Keepalive interval

  // Send the first secure message
  const char* hello_msg = "Hello EC2, this is ESP32 (ESP-IDF) via USMP!";
  if (usmp_send(&ctx, (const uint8_t*)hello_msg, strlen(hello_msg)) == 0) {
    ESP_LOGI(TAG, "Initial hello message sent.");
  }

  uint32_t last_send_time = xTaskGetTickCount();
  uint32_t send_interval = pdMS_TO_TICKS(5000); // Send a message every 5 seconds
  int message_count = 0;

  // ── Main loop — keepalive + reconnect ─────────────────────────────────────
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(100));

    // Read incoming messages if available
    if (ctx.transport.available && ctx.transport.available(&ctx.transport) > 0) {
      uint8_t rx_buf[USMP_MAX_DATA_LEN * USMP_MAX_FRAMES + 1];
      int n = usmp_recv(&ctx, rx_buf, USMP_MAX_DATA_LEN * USMP_MAX_FRAMES);
      if (n > 0) {
        rx_buf[n] = '\0';
        ESP_LOGI(TAG, "Received from EC2: %s", (char*)rx_buf);
      }
    }

    // Handle periodic message transmission
    uint32_t current_time = xTaskGetTickCount();
    if (current_time - last_send_time >= send_interval) {
      last_send_time = current_time;
      message_count++;

      char payload[100];
      snprintf(payload, sizeof(payload), "ESP32 ESP-IDF Ping #%d (Uptime: %lds)", 
               message_count, (long)(current_time * portTICK_PERIOD_MS / 1000));
      
      ESP_LOGI(TAG, "Sending: %s", payload);
      if (usmp_send(&ctx, (const uint8_t*)payload, strlen(payload)) == 0) {
        ESP_LOGI(TAG, "  Sent successfully (encrypted).");
      } else {
        ESP_LOGE(TAG, "  Send failed. Session might be reconnecting...");
      }
    }

    if (usmp_keepalive_tick(&ctx) == 0) continue;

    // ── Connection lost — reconnect ───────────────────────────────────────
    ESP_LOGW(TAG, "Connection lost, reconnecting...");

    int backoff_ms = 2000;
    while (usmp_reconnect(&ctx) != 0) {
      ESP_LOGW(TAG, "Reconnect failed, retrying in %d ms...", backoff_ms);
      vTaskDelay(pdMS_TO_TICKS(backoff_ms));
      if (backoff_ms < 30000) backoff_ms *= 2;  // exponential backoff
    }

    ESP_LOGI(TAG, "Reconnected successfully!");
  }
}
