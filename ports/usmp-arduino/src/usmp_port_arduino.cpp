#include <Arduino.h>

#include "usmp_port.h"

#ifdef ESP32
#include <esp_mac.h>
#include <esp_random.h>
#endif

extern "C" {

int usmp_port_get_device_id(uint8_t* out, size_t len) {
  if (!out || len < 6) return -1;
#ifdef ESP32
  return esp_read_mac(out, ESP_MAC_WIFI_STA) == ESP_OK ? 0 : -1;
#else
#ifndef USMP_INSECURE_FALLBACK
#error \
    "Generic Arduino platforms do not have a secure hardware unique ID source. Define USMP_INSECURE_FALLBACK to bypass this check for testing/development."
#endif
  // Generic fallback: derive from analog noise — not cryptographically unique
  for (int i = 0; i < 6; i++) out[i] = (uint8_t)(analogRead(A0) ^ analogRead(A1) ^ (uint8_t)i);
  return 0;
#endif
}

int usmp_port_random(uint8_t* out, size_t len) {
  if (!out) return -1;
#ifdef ESP32
  for (size_t i = 0; i < len; i++) out[i] = (uint8_t)(esp_random() & 0xFF);
#else
#ifndef USMP_INSECURE_FALLBACK
#error \
    "Generic Arduino platforms do not have a secure hardware random source. Define USMP_INSECURE_FALLBACK to bypass this check for testing/development."
#endif
  // Generic fallback: analog noise — acceptable only for dev
  for (size_t i = 0; i < len; i++) out[i] = (uint8_t)(analogRead(A0) ^ (uint8_t)micros());
#endif
  return 0;
}

void usmp_port_delay_ms(uint32_t ms) { delay(ms); }

uint32_t usmp_port_millis(void) { return millis(); }

void usmp_port_log(char level, const char* tag, const char* msg) {
  Serial.printf("[%c][%s] %s\n", level, tag, msg);
}

}  // extern "C"