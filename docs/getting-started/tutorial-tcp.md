# Tutorial 1: Your First TCP Tunnel

This tutorial walks you through establishing a secure, mutually authenticated TCP tunnel between a Python gateway server and an ESP32 or Arduino client.

---

## 1. The Python Gateway Server

First, let's create a minimal TCP server. This server will listen for incoming device connections, verify their identities via the Pre-Shared Key (PSK), and establish secure sessions.

Create a file named `server_tcp.py` on your gateway/laptop:

```python
import asyncio
from usmp import USMPServer, USMPSession, ConnectionClosedError

# Use the secure PSK generated during the installation phase
# (For testing, you can use a development string)
PSK = b"usmp-dev-psk-change-me-before-prod"

# Initialize the server to listen on TCP port 9000
server = USMPServer(host="0.0.0.0", port=9000, psk=PSK)

@server.on_session
async def handle_device(session: USMPSession):
    print(f"[*] Secure session established! ID: {session.session_id}")
    print(f"[*] Device hardware ID: {session.device_id}")

    try:
        while True:
            # 1. Wait for a decrypted message from the client
            data = await session.recv()
            message = data.decode("utf-8")
            print(f"[RX] {session.device_id[:8]}... says: {message}")

            # 2. Reply with an encrypted confirmation
            reply = f"Acknowledged: {message}"
            await session.send(reply.encode("utf-8"))
            print(f"[TX] Sent encrypted reply to {session.device_id[:8]}...")

    except ConnectionClosedError:
        print(f"[-] Session closed for device: {session.device_id}")

async def main():
    print("[+] Starting USMP TCP Server on port 9000...")
    await server.serve()

if __name__ == "__main__":
    asyncio.run(main())
```

Run the server in your terminal:
```bash
python server_tcp.py
```
It will block and wait for incoming device handshakes:
`[+] Starting USMP TCP Server on port 9000...`

---

## 2. The Client: ESP32 (ESP-IDF Component)

Next, let's configure a native C client to connect to your gateway.

### Step A: Configure Project Dependencies
Ensure `metaloomlabs/usmp` is registered in your `main/idf_component.yml` (as detailed in the Installation guide).

### Step B: Write your application code
Create or update `main/app.c` with the following code. Replace `[GATEWAY_IP_ADDRESS]` with the IP address of your gateway machine.

```c
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usmp.h"
#include "usmp_transport.h"
#include "wifi.h" // Replace with your WiFi initialization logic

static const char *TAG = "USMP_TCP_CLIENT";

// Define the Pre-Shared Key (must match the server's key!)
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

    ESP_LOGI(TAG, "Connecting TCP socket to gateway...");
    if (usmp_transport_tcp_init(&transport, "[GATEWAY_IP_ADDRESS]", 9000) != 0) {
        ESP_LOGE(TAG, "Failed to connect TCP transport.");
        return;
    }

    ESP_LOGI(TAG, "Initiating secure mutual handshake...");
    if (usmp_connect(&ctx, &transport) != 0) {
        ESP_LOGE(TAG, "USMP handshake failed!");
        return;
    }
    ESP_LOGI(TAG, "Tunnel secured! Session Active.");

    // Send an encrypted greeting
    const char *payload = "Hello Gateway! Secure TCP message.";
    if (usmp_send(&ctx, (const uint8_t *)payload, strlen(payload)) == 0) {
        ESP_LOGI(TAG, "Encrypted message sent successfully!");
    }

    // Wait for a secure response
    uint8_t rx_buffer[256];
    int bytes_received = usmp_recv(&ctx, rx_buffer, sizeof(rx_buffer) - 1);
    if (bytes_received > 0) {
        rx_buffer[bytes_received] = '\0';
        ESP_LOGI(TAG, "Decrypted server reply: %s", (char *)rx_buffer);
    }

    // Close the session gracefully
    usmp_close(&ctx);
    ESP_LOGI(TAG, "Session closed.");
}
```

Build, flash, and run the serial monitor:
```bash
idf.py build flash monitor
```

---

## 3. The Client: Arduino (ESP32 Wrapper)

If you are using the Arduino framework, you can achieve the same connection using the C++ `USMPClient` wrapper.

Create a new sketch in the Arduino IDE and paste:

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

    // WiFi connection, socket dial, and cryptographic handshake in one call!
    if (!usmp.begin(USMP::TCP(GATEWAY_IP, 9000).wifi(WIFI_SSID, WIFI_PASS))) {
        Serial.println("USMP Connection failed! Check PSK and Server status.");
        return;
    }

    Serial.println("TCP tunnel established successfully!");
    Serial.println("Device MAC:  " + usmp.deviceId());
    Serial.println("Session ID: " + usmp.sessionId());

    // Send encrypted message
    usmp.send("Hello Gateway from Arduino TCP!");
}

void loop() {
    // Keep engine alive: handles internal drivers and checks for incoming packets
    usmp.maintain(); 

    // Poll for secure incoming packets
    if (usmp.available()) {
        String message = usmp.read();
        Serial.println("Decrypted incoming: " + message);
    }
}
```

Upload the sketch to your board and open the **Serial Monitor** at `115200` baud.

---

## Next Steps

Now that you have a secure TCP connection up and running, let's explore connectionless transmission:
*   **[Tutorial 2: Going Connectionless (UDP)](tutorial-udp.md)** — Transition to a UDP transport while maintaining the exact same security guarantees.
