#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Platform hooks ────────────────────────────────────────────────────────────
// Implement these in usmp_port_<platform>.c for your target board.
// ESP32 implementation: port/usmp_port_esp32.c
// Arduino:              port/usmp_port_arduino.cpp
// STM32:                port/usmp_port_stm32.c

/**
 * Get a unique device identifier.
 * On ESP32: WiFi STA MAC address (6 bytes)
 * On STM32: 96-bit UID (12 bytes, use first 6)
 * On Arduino: chip-specific UID
 *
 * @param out  Buffer to write device ID into
 * @param len  Length of out buffer (minimum 6 bytes)
 * @return     0 on success, -1 on failure
 */
int usmp_port_get_device_id(uint8_t* out, size_t len);

/**
 * Fill buffer with cryptographically random bytes.
 * On ESP32: esp_fill_random()
 * On STM32: HAL_RNG_GenerateRandomNumber()
 * On Arduino: hardware RNG or analog noise
 *
 * @param out  Buffer to fill
 * @param len  Number of random bytes to generate
 * @return     0 on success, -1 on failure
 */
int usmp_port_random(uint8_t* out, size_t len);

/**
 * Delay for a given number of milliseconds.
 * On ESP32: vTaskDelay(pdMS_TO_TICKS(ms))
 * On STM32: HAL_Delay(ms)
 * On Arduino: delay(ms)
 *
 * @param ms  Milliseconds to delay
 */
void usmp_port_delay_ms(uint32_t ms);

/**
 * Get current time in milliseconds since boot.
 * Used for session TTL and keepalive tracking.
 * On ESP32: esp_timer_get_time() / 1000
 * On STM32: HAL_GetTick()
 * On Arduino: millis()
 *
 * @return  Milliseconds since boot (wraps at UINT32_MAX)
 */
uint32_t usmp_port_millis(void);

/**
 * Log a message.
 * On ESP32: ESP_LOGI/LOGE
 * On STM32: UART printf
 * On Arduino: Serial.println
 *
 * @param level  'I' = info, 'W' = warn, 'E' = error
 * @param tag    Module tag string
 * @param msg    Message string
 */
void usmp_port_log(char level, const char* tag, const char* msg);

// Convenience macros ────────────────────────────────────────────────────────
#define USMP_LOGI(tag, msg) usmp_port_log('I', tag, msg)
#define USMP_LOGW(tag, msg) usmp_port_log('W', tag, msg)
#define USMP_LOGE(tag, msg) usmp_port_log('E', tag, msg)

/*
 * USMP_LOGD — debug-level logging (verbose, disabled in production builds).
 * Implement usmp_port_log with level 'D' in your port if you want debug output,
 * or define USMP_DISABLE_DEBUG to make this a no-op.
 */
#ifdef USMP_DISABLE_DEBUG
#define USMP_LOGD(tag, msg) ((void)0)
#else
#define USMP_LOGD(tag, msg) usmp_port_log('D', tag, msg)
#endif

#ifdef __cplusplus
}
#endif