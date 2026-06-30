# Tutorial 2: Going Connectionless (UDP)

In this tutorial, you will learn how to configure USMP to operate over a connectionless UDP transport layer. 

Although UDP is inherently unreliable (packets can be dropped or arrive out of order), USMP builds a lightweight reliability wrapper on top of it. You receive the speed and battery efficiency of UDP, combined with transport-level packet ordering, duplicate discard, and secure frame validation.

---

## 1. The Python Gateway Server

Creating a UDP-based gateway in Python is as simple as adding `protocol="udp"` to the `USMPServer` constructor.

Create `server_udp.py`:

```python
import asyncio
from usmp import USMPServer, USMPSession, ConnectionClosedError

PSK = b"usmp-dev-psk-change-me-before-prod"

# Initialize the server over UDP
server = USMPServer(host="0.0.0.0", port=9000, psk=PSK, protocol="udp")

@server.on_session
async def handle_device(session: USMPSession):
    print(f"[*] Secure UDP Session established! ID: {session.session_id}")
    print(f"[*] Device hardware ID: {session.device_id}")

    try:
        while True:
            # Receive decrypted data
            data = await session.recv()
            message = data.decode("utf-8")
            print(f"[RX-UDP] {session.device_id[:8]}... says: {message}")

            # Send encrypted reply
            reply = f"UDP Acknowledged: {message}"
            await session.send(reply.encode("utf-8"))
            print(f"[TX-UDP] Sent encrypted reply to {session.device_id[:8]}...")

    except ConnectionClosedError:
        print(f"[-] UDP Session closed for device: {session.device_id}")

async def main():
    print("[+] Starting USMP UDP Server on port 9000...")
    await server.serve()

if __name__ == "__main__":
    asyncio.run(main())
```

Run the server in your terminal:
```bash
python server_udp.py
```
It will block and output:
`[+] Starting USMP UDP Server on port 9000...`

---

## 2. The Client: ESP32 (ESP-IDF Component)

To swap from TCP to UDP on native C, you replace the TCP initialization function with its UDP counterpart. All other session functions (`usmp_connect`, `usmp_send`, `usmp_recv`) remain completely identical!

Create or update `main/app.c`:

```c
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usmp.h"
#include "usmp_transport.h"
#include "wifi.h" // Replace with your WiFi initialization logic

static const char *TAG = "USMP_UDP_CLIENT";

static const uint8_t PSK[] = "usmp-dev-psk-change-me-before-prod";

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing Wi-Fi...");
    if (!wifi_init()) {
        ESP_LOGE(TAG, "Wi-Fi connection failed!");
        return;
    }

    usmp_t ctx = {0};
    usmp_transport_t transport = {0};

    // Configure runtime credentials
    ctx.psk = PSK;
    ctx.psk_len = sizeof(PSK) - 1; // Exclude null terminator

    ESP_LOGI(TAG, "Initializing UDP socket to gateway...");
    // Use the UDP transport initializer
    if (usmp_transport_udp_init(&transport, "[GATEWAY_IP_ADDRESS]", 9000) != 0) {
        ESP_LOGE(TAG, "Failed to connect UDP transport.");
        return;
    }

    ESP_LOGI(TAG, "Initiating secure mutual handshake over UDP...");
    if (usmp_connect(&ctx, &transport) != 0) {
        ESP_LOGE(TAG, "USMP handshake failed!");
        return;
    }
    ESP_LOGI(TAG, "Tunnel secured! UDP Session Active.");

    // Send an encrypted message
    const char *payload = "Hello Gateway! Secure UDP telemetry.";
    if (usmp_send(&ctx, (const uint8_t *)payload, strlen(payload)) == 0) {
        ESP_LOGI(TAG, "Encrypted message sent!");
    }

    // Wait for a secure response
    uint8_t rx_buffer[256];
    int bytes_received = usmp_recv(&ctx, rx_buffer, sizeof(rx_buffer) - 1);
    if (bytes_received > 0) {
        rx_buffer[bytes_received] = '\0';
        ESP_LOGI(TAG, "Decrypted server UDP reply: %s", (char *)rx_buffer);
    }

    // Close the session gracefully
    usmp_close(&ctx);
    ESP_LOGI(TAG, "UDP Session closed.");
}
```

Build, flash, and run the monitor:
```bash
idf.py build flash monitor
```

---

## 3. The Client: Arduino (ESP32 Wrapper)

On Arduino, you change the transport driver parameter inside `begin()` from `USMP::TCP(...)` to `USMP::UDP(...)`.

Create a new sketch in your Arduino IDE:

```cpp
#include <USMP.h>

#define PSK           "usmp-dev-psk-change-me-before-prod"
#define GATEWAY_IP    "[GATEWAY_IP_ADDRESS]"
#define WIFI_SSID     "YourNetworkSSID"
#define WIFI_PASS     "YourNetworkPassword"

// Initialize client with Pre-Shared Key
USMPClient usmp(PSK);

void setup() {
    Serial.begin(115200);

    // Swap to USMP::UDP inside the begin method
    if (!usmp.begin(USMP::UDP(GATEWAY_IP, 9000).wifi(WIFI_SSID, WIFI_PASS))) {
        Serial.println("USMP UDP connection failed!");
        return;
    }

    Serial.println("UDP tunnel established successfully!");
    Serial.println("Device MAC:  " + usmp.deviceId());
    Serial.println("Session ID: " + usmp.sessionId());

    // Send encrypted message
    usmp.send("Hello Gateway from Arduino UDP!");
}

void loop() {
    // Keep engine alive: handles internal drivers and checks for incoming packets
    usmp.maintain(); 

    // Poll for secure incoming packets
    if (usmp.available()) {
        String message = usmp.read();
        Serial.println("Decrypted UDP RX: " + message);
    }
}
```

Upload the sketch to your board and open the **Serial Monitor** at `115200` baud.

---

## Next Steps

Now that you have successfully mastered both TCP and UDP transports, you are ready to prepare your application for a real-world deployment. Proceed to the hardening guide:
*   **[Tutorial 3: Production Hardening](production-hardening.md)** — Learn how to load credentials securely from storage, handle network drops, perform keepalives, and manage reconnection loops.
