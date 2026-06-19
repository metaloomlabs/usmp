# Quick Start — Arduino (ESP32)

Get a secure USMP session running between your ESP32 (running Arduino) and a Python gateway in under 10 minutes.

## Prerequisites

- ESP32 Development Board (e.g. NodeMCU, ESP32 DevKitC)
- Arduino IDE (v2.0+) or PlatformIO
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32) installed
- A WiFi network both your computer and ESP32 can join

---

## Step 1 — Install the USMP Library

USMP is packaged as a standard Arduino library. You can install it in one of two ways:

=== "Add ZIP Library (Recommended)"
    1. Locate the `usmp-arduino.zip` file in the root of the USMP project.
    2. In the Arduino IDE, go to **Sketch** ➔ **Include Library** ➔ **Add .ZIP Library...**
    3. Select `usmp-arduino.zip`.

=== "Manual Copy"
    Copy the `ports/usmp-arduino` folder directly into your local `libraries/` directory:
    - **Windows**: `Documents/Arduino/libraries/USMP`
    - **macOS/Linux**: `~/Arduino/libraries/USMP`

---

## Step 2 — Write Your Sketch

Create a new sketch in the Arduino IDE and paste the following code:

```cpp title="basic.ino"
#include <USMP.h>

// ── Config ────────────────────────────────────────────────────────────────────
#define PSK        "usmp-dev-psk-change-me-before-prod"
#define SERVER_IP  "192.168.137.1" // Change to your Python gateway IP address
#define WIFI_SSID  "YourNetworkSSID"
#define WIFI_PASS  "YourNetworkPassword"

// Create USMP client instance with pre-shared key
USMPClient usmp(PSK);

void setup() {
    Serial.begin(115200);

    // Connect to WiFi, establish TCP connection, and perform USMP handshake
    if (!usmp.begin(USMP::TCP(SERVER_IP).wifi(WIFI_SSID, WIFI_PASS))) {
        Serial.println("USMP connect failed — check server and PSK");
        return;
    }

    Serial.println("Connected!");
    Serial.println("Device ID:  " + usmp.deviceId());
    Serial.println("Session ID: " + usmp.sessionId());

    // Send an encrypted message once connected
    usmp.send("hello from arduino");
}

void loop() {
    // Keep connection alive & handle reconnects automatically
    usmp.maintain(); 

    // Read incoming messages
    if (usmp.available()) {
        String msg = usmp.read();
        Serial.println("RX: " + msg);
    }
}
```

---

## Step 3 — Run the Python Gateway

To set up the server receiving side, refer to the [Quick Start (Python)](quickstart-python.md) guide. Start the Python server:

```bash
python server.py
```

---

## Step 4 — Upload and Monitor

1. Select your ESP32 board and port in the Arduino IDE.
2. Upload the sketch.
3. Open the **Serial Monitor** (set baud rate to `115200`).

### Expected Serial Output

```txt
[USMP] Connecting to WiFi: YourNetworkSSID
[USMP] WiFi connected — IP: 192.168.137.50
[USMP] TCP Connected
Connected!
Device ID:  ab:cd:ef:01:02:03
Session ID: 5f3b7c2a8e9d0a1b2c3d4e5f6a7b8c9d
```

On the Python server console, you should see:
```txt
[USMP] Session established: device=ab:cd:ef:01:02:03 session=5f3b7c2a8e9d0a1b2c3d4e5f6a7b8c9d
Device connected: ab:cd:ef:01:02:03
Received: b'hello from arduino'
```

---

## Under the Hood

Calling `usmp.begin()` initiates the USMP cryptographic handshake:
1. **Exchange Ephemeral Keys**: Generates a temporary X25519 key pair to establish a forward-secret shared secret.
2. **Mutual Authentication**: Verifies that both sides possess the exact Pre-Shared Key (PSK) using HMAC-SHA256.
3. **Session Key Derivation**: Derives the final symmetric AES-256-GCM key used for all post-handshake traffic.
4. **Session Maintenance**: The `usmp.maintain()` function handles keepalive pings and automatically reconnects with an exponential backoff if connection drops.
