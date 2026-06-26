# Quick Start — Getting ESP32 Online and Secure

Welcome to the ESP32 quickstart! In this guide, we will hook up your ESP32 board to a secure Python gateway in under 10 minutes. By the end, you'll have a fully encrypted, mutually authenticated tunnel running over a raw TCP socket.

## Prerequisites

Before we start, make sure you have:

* **An ESP32 DevKit** (any standard variant).
* **ESP-IDF v5.0 or later** installed and configured in your environment.
* **Python 3.11+** installed on your development machine.
* **A local network** (Wi-Fi or mobile hotspot) that both your laptop and ESP32 can connect to.

## Step 1 — Adding the USMP Component

USMP fits cleanly into the ESP-IDF build system. Let's register it:

1. In your project's root `CMakeLists.txt`, register the USMP component directory:

   ```cmake
   # Tell CMake where the USMP component lives
   set(EXTRA_COMPONENT_DIRS
       "/path/to/usmp/ports/usmp-esp32"
   )

   include($ENV{IDF_PATH}/tools/cmake/project.cmake)
   project(your_project)
   ```

2. In your application's source directory (usually `main/CMakeLists.txt`), declare your dependencies:

   ```cmake
   idf_component_register(
       SRCS "app.c" "wifi.c"
       INCLUDE_DIRS "."
       REQUIRES usmp-esp32
       PRIV_REQUIRES nvs_flash esp_wifi
   )
   ```

## Step 2 — Writing Your Application

Let's write a simple application that connects to the gateway, establishes a secure session, sends a telemetry message, and waits for a response.

```c title="main/app.c"
#include "usmp.h"
#include "usmp_transport.h"
#include "wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "USMP_APP";

void app_main(void)
{
    // 1. Initialize your Wi-Fi interface (implement this based on your standard Wi-Fi flow)
    wifi_init();

    // 2. Initialize the TCP transport to point to your gateway server
    usmp_transport_t transport = {0};
    if (usmp_transport_tcp_init(&transport, "192.168.1.100", 9000) != 0) {
        ESP_LOGE(TAG, "Failed to connect TCP socket to the gateway");
        return;
    }

    // 3. Set up the USMP context
    usmp_t ctx = {0};
    
    // Define the secret Pre-Shared Key (must match the server's key!)
    static const uint8_t s_psk[] = "usmp-dev-psk-change-me-before-prod";
    ctx.psk     = s_psk;
    ctx.psk_len = sizeof(s_psk) - 1; // Exclude the null terminator

    // 4. Kick off the mutual handshake!
    ESP_LOGI(TAG, "Starting secure USMP handshake...");
    if (usmp_connect(&ctx, &transport) != 0) {
        ESP_LOGE(TAG, "USMP handshake failed!");
        return;
    }

    ESP_LOGI(TAG, "Secure session established successfully!");

    // 5. Send an encrypted message
    const char *msg = "Hello Gateway! This is ESP32 speaking securely.";
    if (usmp_send(&ctx, (const uint8_t *)msg, strlen(msg)) == 0) {
        ESP_LOGI(TAG, "Encrypted message sent!");
    }

    // 6. Receive a secure reply
    uint8_t buf[256];
    int len = usmp_recv(&ctx, buf, sizeof(buf));
    if (len > 0) {
        ESP_LOGI(TAG, "Received encrypted reply: %.*s", len, buf);
    }

    // 7. Clean up and close the session gracefully
    usmp_close(&ctx);
    ESP_LOGI(TAG, "Session closed.");
}
```

> [!WARNING]
> **Production Key Management**
> Never hardcode production Pre-Shared Keys directly in your application source code! Instead, load the PSK at runtime from the ESP32's non-volatile storage (NVS) using `nvs_get_blob()`, or provision it during manufacturing.

## Step 3 — Build and Flash

Build your project, flash it onto your ESP32, and launch the monitor to watch the logs:

```bash
idf.py build flash monitor
```

## Step 4 — Run the Python Gateway

To capture the connection, you'll need the gateway running on your laptop. Jump over to the [Python Quick Start](quickstart-python.md) to launch the receiver server.

## Under the Hood: What Just Happened?

When you called `usmp_connect()`, USMP performed a secure, 4-step cryptographic handshake:

```text
ESP32 (Client)                                      Gateway (Server)
      │                                                     │
      │ ─── 1. HELLO (device_id, pub_C) ──────────────────> │
      │                                                     │
      │ <── 2. CHALLENGE (nonce, pub_S) ─────────────────── │
      │                                                     │ [X25519 Key Exchange]
      │                                                     │ [HKDF-SHA256 derivation]
      │ ─── 3. HELLO_ACK (HMAC_client) ───────────────────> │
      │                                                     │ [Verify Client HMAC]
      │ <── 4. SESSION_OK (session_id, HMAC_server) ─────── │
      │                                                     │
      └──────────────── Encrypted Session ──────────────────┘
```

1. **The Intro (`HELLO`)**: The ESP32 sends its hardware Device ID along with a freshly generated ephemeral X25519 public key (`pub_C`).
2. **The Challenge (`CHALLENGE`)**: The gateway replies with a random salt (`nonce`) and its own ephemeral X25519 public key (`pub_S`).
3. **The Client Proof (`HELLO_ACK`)**: Both sides calculate a shared secret via Diffie-Hellman (`X25519`). Using HKDF-SHA256, they derive temporary keys. The ESP32 then calculates an HMAC using the Pre-Shared Key (PSK), cryptographically binding `pub_C` and `pub_S` to the HMAC to prove its identity and prevent Man-in-the-Middle (MITM) key-swapping.
4. **The Server Confirmation (`SESSION_OK`)**: The gateway verifies the client's proof, computes its own HMAC (also binding the keys), and sends back a unique Session ID.

Now, both sides share an identical AES-256-GCM symmetric session key. **This key was never sent over the air.** If an eavesdropper intercepted the entire handshake, they cannot compute the session key without knowing the secret PSK.

Every subsequent data packet is encrypted with AES-256-GCM using deterministic nonces. If anyone tampers with the ciphertext, decryption fails immediately, protecting your device from unauthorized commands!
