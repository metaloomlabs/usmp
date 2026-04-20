# ESP32 + Python Server

A complete working example of an ESP32 sending encrypted sensor data
to a Python gateway.

## What it does

1. ESP32 connects to WiFi
2. ESP32 establishes a DXP session with the Python server
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
from dxp import DXPServer, DXPSession

PSK    = b"dxp-dev-psk-change-me-before-prod"
HOST   = "192.168.137.1"   # hotspot gateway IP
PORT   = 9000

server = DXPServer(host=HOST, port=PORT, psk=PSK)

@server.on_session
async def handle(session: DXPSession):
    print(f"[SESSION] device={session.device_id} session={session.session_id}")
    while True:
        data = await session.recv()
        print(f"[RX] {data!r}")

asyncio.run(server.serve())
```

## ESP32 application

```c title="firmware/main/app.c"
#include "dxp.h"
#include "dxp_transport.h"
#include "wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "APP";

void app_main(void)
{
    wifi_init();

    dxp_transport_t transport = {0};
    for (int i = 1; i <= 10; i++) {
        if (dxp_transport_tcp_init(&transport, "192.168.137.1", 9000) == 0)
            break;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    dxp_t ctx = {0};
    if (dxp_connect(&ctx, &transport) != 0) return;

    dxp_send(&ctx, (uint8_t *)"hello encrypted world", 21);

    while (dxp_is_connected(&ctx)) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        dxp_send(&ctx, (uint8_t *)"ping", 4);
    }

    dxp_close(&ctx);
}
```

## Expected output

**Python server:**

```
[DXP] Listening on 192.168.137.1:9000
[DXP] TCP connected: ('192.168.137.x', xxxxx)
[DXP] Session established: device=aa:bb:cc:dd:ee:ff session=12345678
[SESSION] device=aa:bb:cc:dd:ee:ff session=12345678
[RX] b'hello encrypted world'
[RX] b'ping'
[RX] b'ping'
```

**ESP32 serial:**

```
I (xxx) DXP_HS: HELLO sent
I (xxx) DXP_HS: CHALLENGE received
I (xxx) DXP_HS: X25519 shared secret computed
I (xxx) DXP_HS: Session key derived
I (xxx) DXP_HS: HELLO_ACK sent
I (xxx) DXP_HS: Server authenticated OK
I (xxx) DXP_HS: SESSION_OK
I (xxx) DXP: Session established
I (xxx) DXP_SESSION: TX seq=0 len=21
I (xxx) APP: Message sent
```
