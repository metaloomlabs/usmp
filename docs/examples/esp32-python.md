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
from usmp import USMPServer, USMPSession

PSK    = b"usmp-dev-psk-change-me-before-prod"
HOST   = "192.168.137.1"   # hotspot gateway IP
PORT   = 9000

server = USMPServer(host=HOST, port=PORT, psk=PSK)

@server.on_session
async def handle(session: USMPSession):
    print(f"[SESSION] device={session.device_id} session={session.session_id}")
    while True:
        data = await session.recv()
        print(f"[RX] {data!r}")

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
    wifi_init();

    usmp_transport_t transport = {0};
    for (int i = 1; i <= 10; i++) {
        if (usmp_transport_tcp_init(&transport, "192.168.137.1", 9000) == 0)
            break;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    usmp_t ctx = {0};
    if (usmp_connect(&ctx, &transport) != 0) return;

    usmp_send(&ctx, (uint8_t *)"hello encrypted world", 21);

    while (usmp_is_connected(&ctx)) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        usmp_send(&ctx, (uint8_t *)"ping", 4);
    }

    usmp_close(&ctx);
}
```

## Expected output

**Python server:**

```
[USMP] Listening on 192.168.137.1:9000
[USMP] TCP connected: ('192.168.137.x', xxxxx)
[USMP] Session established: device=aa:bb:cc:dd:ee:ff session=12345678
[SESSION] device=aa:bb:cc:dd:ee:ff session=12345678
[RX] b'hello encrypted world'
[RX] b'ping'
[RX] b'ping'
```

**ESP32 serial:**

```
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
