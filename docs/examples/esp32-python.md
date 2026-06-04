# ESP32 + Python Server

A complete working example of an ESP32 sending encrypted sensor data
to a Python gateway.

## What it does

1. ESP32 connects to WiFi
2. ESP32 establishes a USMP session with the Python server
3. ESP32 sends `"hello encrypted world"` then pings every 5 seconds
4. Python server receives, decrypts, and prints each message

## Files

```

examples/
  python_server/
    esp32_server.py   ← Python gateway
firmware/
  main/
    app.c             ← ESP32 application
```

## Python gateway

```python title="examples/python_server/esp32_server.py"
import asyncio
from usmp import USMPServer, USMPSession, ConnectionClosedError

PSK = b"usmp-dev-psk-change-me-before-prod"
HOST = "0.0.0.0"
PORT = 9000

server = USMPServer(host=HOST, port=PORT, psk=PSK)


@server.on_session
async def handle(session: USMPSession):
    print(f"[SESSION] device={session.device_id} session={session.session_id}")
    try:
        while True:
            data = await session.recv()
            text = data.decode().strip()
            try:
                value = int(text)
                result = value * 2
                print(f"[RX] {value} → sending back {result}")
                await session.send(str(result).encode())
            except ValueError:
                print(f"[SKIP] non-numeric: {text!r}")
    except ConnectionClosedError:
        print(f"[CLOSED] {session.device_id}")


asyncio.run(server.serve())
```

## ESP32 application

```c title="firmware/main/app.c"
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
    ESP_LOGI(TAG, "Initializing Wi-Fi");
    if (!wifi_init())
    {
        ESP_LOGE(TAG, "Wi-Fi init failed");
        return;
    }

    const char *server_ip = "192.168.137.1";
    const int port = USMP_DEFAULT_PORT;

    usmp_t ctx = {0};
    usmp_transport_t transport = {0};

    // ── Initial connect with retries ──────────────────────────────────────────
    bool connected = false;
    for (int attempt = 1; attempt <= USMP_CONNECT_RETRIES; ++attempt)
    {
        if (usmp_transport_tcp_init(&transport, server_ip, port) == 0)
        {
            ESP_LOGI(TAG, "TCP connected (attempt %d)", attempt);
            connected = true;
            break;
        }
        ESP_LOGW(TAG, "Attempt %d failed, retrying...", attempt);
        vTaskDelay(pdMS_TO_TICKS(USMP_CONNECT_RETRY_MS));
    }

    if (!connected)
    {
        ESP_LOGE(TAG, "Unable to connect to %s:%d", server_ip, port);
        return;
    }

    if (usmp_connect(&ctx, &transport) != 0)
    {
        ESP_LOGE(TAG, "USMP connect failed");
        return;
    }

    ctx.keepalive_ms = 15000; // PING every 15s if idle

    const char *msg = "hello encrypted world";
    if (usmp_send(&ctx, (const uint8_t *)msg, strlen(msg)) == 0)
        ESP_LOGI(TAG, "Message sent");

    // ── Main loop — keepalive + reconnect ─────────────────────────────────────
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (usmp_keepalive_tick(&ctx) == 0)
            continue;

        // ── Connection lost — reconnect ───────────────────────────────────────
        ESP_LOGW(TAG, "Connection lost, reconnecting...");

        int backoff_ms = 2000;
        while (usmp_reconnect(&ctx) != 0)
        {
            ESP_LOGW(TAG, "Reconnect failed, retrying in %dms...", backoff_ms);
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            if (backoff_ms < 30000)
                backoff_ms *= 2; // exponential backoff, cap at 30s
        }

        ESP_LOGI(TAG, "Reconnected");

        // Re-send hello after new session
        if (usmp_send(&ctx, (const uint8_t *)msg, strlen(msg)) == 0)
            ESP_LOGI(TAG, "Message sent");
    }
}
```

## Expected output

**Python server:**

```
[USMP] Listening on 0.0.0.0:9000
[USMP] TCP connected: ('192.168.137.x', xxxxx)
[USMP] Session established: device=aa:bb:cc:dd:ee:ff session=12345678
[SESSION] device=aa:bb:cc:dd:ee:ff session=12345678
[SKIP] non-numeric: 'hello encrypted world'
```

**ESP32 serial:**

```
I (xxx) APP: Initializing Wi-Fi
I (xxx) APP: TCP connected (attempt 1)
I (xxx) USMP_HS: HELLO sent
I (xxx) USMP_HS: CHALLENGE received
I (xxx) USMP_HS: X25519 shared secret computed
I (xxx) USMP_HS: Session key derived
I (xxx) USMP_HS: HELLO_ACK sent
I (xxx) USMP_HS: Server authenticated OK
I (xxx) USMP_HS: SESSION_OK
I (xxx) USMP: Session established
I (xxx) USMP_SESSION: TX seq=0 len=21
I (xxx) APP: Message sent
```
