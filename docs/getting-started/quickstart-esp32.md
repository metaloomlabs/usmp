# Quick Start — ESP32

Get a secure USMP session running between your ESP32 and a Python gateway in under 10 minutes.

## Prerequisites

- ESP32 DevKit (any variant)
- ESP-IDF v5.0 or later
- Python 3.11+
- A WiFi network both your laptop and ESP32 can join (or use your laptop's hotspot)

## Step 1 — Add the USMP component

In your ESP-IDF project's `CMakeLists.txt`:

```cmake
set(EXTRA_COMPONENT_DIRS
    "/path/to/usmp/ports/usmp-esp32"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(your_project)
```

In your `main/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "app.c" "wifi.c"
    INCLUDE_DIRS "."
    REQUIRES usmp-esp32
    PRIV_REQUIRES nvs_flash esp_wifi
)
```

## Step 2 — Write your application

```c title="main/app.c"
#include "usmp.h"
#include "usmp_transport.h"
#include "wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "APP";

void app_main(void)
{
    // Connect to WiFi first
    wifi_init();

    // Initialize TCP transport
    usmp_transport_t transport = {0};
    if (usmp_transport_tcp_init(&transport, "192.168.1.100", 9000) != 0) {
        ESP_LOGE(TAG, "TCP connect failed");
        return;
    }

    // USMP handshake — mutual auth + key exchange
    usmp_t ctx = {0};
    if (usmp_connect(&ctx, &transport) != 0) {
        ESP_LOGE(TAG, "USMP connect failed");
        return;
    }

    ESP_LOGI(TAG, "Session established!");

    // Send encrypted data
    const char *msg = "hello from ESP32";
    usmp_send(&ctx, (const uint8_t *)msg, strlen(msg));

    // Receive response
    uint8_t buf[256];
    int len = usmp_recv(&ctx, buf, sizeof(buf));
    if (len > 0) {
        ESP_LOGI(TAG, "Received: %.*s", len, buf);
    }

    usmp_close(&ctx);
}
```

!!! tip "PSK Configuration"
    The default PSK is `usmp-dev-psk-change-me-before-prod`.
    Change it by defining `USMP_PSK` before including `usmp.h`:
    ```c
    #define USMP_PSK "your-secret-key-here"
    #include "usmp.h"
    ```

## Step 3 — Build and flash

```bash
idf.py build flash monitor```


## Step 4 — Run the gateway

See [Quick Start (Python)](quickstart-python.md) to set up the receiving end.


## What just happened?

When `usmp_connect` runs, USMP performs a full 4-step handshake:

1. **HELLO** — ESP32 sends its device ID and an ephemeral X25519 public key
2. **CHALLENGE** — Gateway sends a random nonce and its own X25519 public key
3. **HELLO_ACK** — ESP32 proves it knows the PSK via HMAC
4. **SESSION_OK** — Gateway proves it knows the PSK via HMAC + sends session ID

Both sides independently derive the same AES-256-GCM session key from the X25519 shared secret via HKDF. The key never travels over the wire.

After the handshake, every frame is encrypted and authenticated. A corrupted or replayed frame is immediately detected and rejected.
