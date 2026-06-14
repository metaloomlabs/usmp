#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usmp_port.h"
#include <stdio.h>
#include <string.h>

int usmp_port_get_device_id(uint8_t *out, size_t len) {
  if (!out || len < 6)
    return -1;
  return esp_read_mac(out, ESP_MAC_WIFI_STA) == ESP_OK ? 0 : -1;
}

int usmp_port_random(uint8_t *out, size_t len) {
  if (!out)
    return -1;
  esp_fill_random(out, len);
  return 0;
}

void usmp_port_delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

uint32_t usmp_port_millis(void) {
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

void usmp_port_log(char level, const char *tag, const char *msg) {
  switch (level) {
  case 'I':
    ESP_LOGI(tag, "%s", msg);
    break;
  case 'W':
    ESP_LOGW(tag, "%s", msg);
    break;
  case 'E':
    ESP_LOGE(tag, "%s", msg);
    break;
  default:
    ESP_LOGI(tag, "%s", msg);
    break;
  }
}