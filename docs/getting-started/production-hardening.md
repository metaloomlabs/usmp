# Tutorial 3: Production Hardening

When moving from a local breadboard prototype to a production environment, you must build robust error handling, secure key storage, and automatic recovery systems. This guide details how to harden both your devices and gateways.

---

## 1. Secure Credential Management (No Hardcoded PSKs)

Hardcoding Pre-Shared Keys directly in your firmware binary makes them vulnerable to reverse engineering and extraction. 

### ESP32: Loading PSK from Non-Volatile Storage (NVS)

On the ESP32, you should store the PSK in a secure, encrypted NVS partition. Here is how to retrieve the key dynamically at runtime:

```c
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "usmp.h"

static const char *TAG = "SECURITY";

bool load_psk_from_nvs(uint8_t *psk_buf, size_t *psk_len) {
    nvs_handle_t my_handle;
    esp_err_t err;

    // 1. Open the "storage" namespace in NVS
    err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return false;
    }

    // 2. Fetch the binary key length
    err = nvs_get_blob(my_handle, "usmp_psk", NULL, psk_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error reading PSK length: %s", esp_err_to_name(err));
        nvs_close(my_handle);
        return false;
    }

    // 3. Read the actual PSK blob
    err = nvs_get_blob(my_handle, "usmp_psk", psk_buf, psk_len);
    nvs_close(my_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error reading PSK payload: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

// In your application startup:
void initialize_usmp_context(usmp_t *ctx) {
    static uint8_t dynamic_psk[32];
    size_t psk_len = sizeof(dynamic_psk);

    if (load_psk_from_nvs(dynamic_psk, &psk_len)) {
        ctx->psk = dynamic_psk;
        ctx->psk_len = psk_len;
        ESP_LOGI(TAG, "Secure PSK loaded from NVS storage.");
    } else {
        ESP_LOGE(TAG, "Critical: PSK could not be loaded!");
    }
}
```

---

## 2. Keepalive Monitoring & Connection Watchdogs

A network connection can drop silently without closing the underlying socket (the "half-open" state). To detect this, USMP provides keepalive heartbeats (`PING` / `PONG`).

*   **ESP-IDF**: You configure the keepalive interval directly on the context:
    ```c
    ctx.keepalive_ms = 15000; // Send a PING frame if no data is sent for 15 seconds
    ```
*   **Arduino**: Set the interval using the helper method **after** `begin()`:
    ```cpp
    usmp.keepalive(15000); // 15 seconds
    ```

---

## 3. Automatic Recovery and Reconnection Loops

When a connection is interrupted, the device must gracefully re-establish the session. Because the session key is ephemeral, a reconnection requires performing a fresh handshake.

### ESP-IDF Reconnection Loop with Exponential Backoff

In ESP-IDF, you must tick the keepalive watchdog inside your main loop and handle reconnection failures with a backoff delay to prevent spamming the gateway:

```c
void app_main(void) {
    // ... initial WiFi setup, transport init, and usmp_connect() ...

    ctx.keepalive_ms = 10000; // 10 second keepalive

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100)); // Relinquish CPU control

        // Check incoming data
        if (ctx.transport.available && ctx.transport.available(&ctx.transport) > 0) {
            uint8_t rx_buf[256];
            int n = usmp_recv(&ctx, rx_buf, sizeof(rx_buf) - 1);
            if (n > 0) {
                rx_buf[n] = '\0';
                ESP_LOGI("APP", "Received message: %s", rx_buf);
            }
        }

        // Tick the keepalive watchdogs.
        // Returns 0 if everything is normal. Returns a negative value on link loss.
        if (usmp_keepalive_tick(&ctx) != 0) {
            ESP_LOGW("APP", "Connection lost! Initiating automatic recovery...");

            int backoff_ms = 2000; // Start with 2 seconds backoff
            const int max_backoff_ms = 60000; // Cap at 60 seconds

            while (usmp_reconnect(&ctx) != 0) {
                ESP_LOGW("APP", "Reconnect failed. Retrying in %dms...", backoff_ms);
                vTaskDelay(pdMS_TO_TICKS(backoff_ms));
                
                // Double the backoff delay
                if (backoff_ms < max_backoff_ms) {
                    backoff_ms *= 2;
                }
            }

            ESP_LOGI("APP", "Session successfully restored!");
        }
    }
}
```

### Arduino Callback-Driven Reconnections

The Arduino `USMPClient` automatically handles keepalive ticks and exponential backoff loops inside the background `usmp.maintain()` task. 

However, you should register callbacks to update your application state (such as flashing an LED or pausing sensor polling while offline):

```cpp
#include <USMP.h>

USMPClient usmp("your-psk");

const int STATUS_LED = 2;

void onConnect() {
    digitalWrite(STATUS_LED, HIGH); // Solid light: Connected
    Serial.println("[APP] Connected!");
    usmp.keepalive(10000);
}

void onDisconnect() {
    digitalWrite(STATUS_LED, LOW); // LED Off: Offline
    Serial.println("[APP] Disconnected, retrying...");
}

void onReconnect() {
    digitalWrite(STATUS_LED, HIGH); // Solid light: Reconnected
    Serial.println("[APP] Reconnected!");
}

void setup() {
    pinMode(STATUS_LED, OUTPUT);
    Serial.begin(115200);

    usmp.onConnect(onConnect);
    usmp.onDisconnect(onDisconnect);
    usmp.onReconnect(onReconnect);

    usmp.begin(USMP::TCP("192.168.1.100"));
}

void loop() {
    usmp.maintain(); // Keep driving the background handler
    delay(10);
}
```

---

## 4. Gateway Hardening (Python SDK)

The Python gateway server contains built-in protections against denial-of-service (DoS) and brute-force handshake attempts:

*   **Failed Handshake Lockout**: If a device IP fails the handshake process (due to an incorrect PSK or protocol violation) multiple times in a row, the server locks out that IP address for **60 seconds**, dropping any incoming packets immediately.
*   **Concurrent Handshake Limits**: To prevent resource exhaustion, the server limits any single IP address to **3 concurrent handshakes** in progress.
*   **Watchdog Task**: The server runs an asynchronous watchdog task on every session. If a connected device stops sending data or keepalive pings for the duration of the `session_timeout` (default: 60 seconds), the server closes the session socket and frees its resources.
