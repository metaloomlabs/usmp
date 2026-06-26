# Quick Start — Arduino (ESP32)

Hello, Arduino developers! In this guide, we'll get a secure USMP session running between your ESP32 board (running Arduino C++) and a Python gateway. You'll be sending and receiving encrypted telemetry in under 10 minutes.

## Prerequisites

Before you start, make sure you have:

* **An ESP32 Development Board** (NodeMCU, ESP32 DevKitC, etc.).
* **Arduino IDE (v2.0+)** or **PlatformIO**.
* **The ESP32 Arduino Core** installed in your IDE.
* **A local network** (Wi-Fi or hotspot) that both your computer and ESP32 can connect to.

## Step 1 — Installing the USMP Library

USMP is packaged as a standard Arduino library. You can install it using one of two methods:

=== "Add ZIP Library (Recommended)"
    1. Locate the pre-built `usmp-*.zip` file in the root of the USMP project (e.g., `usmp-0.4.7-arduino.zip`).
    2. Open your Arduino IDE.
    3. Navigate to **Sketch** ➔ **Include Library** ➔ **Add .ZIP Library...**
    4. Select the zip file.

=== "Manual Copy"
    Copy the `ports/usmp-arduino` folder directly into your local Arduino libraries directory:
    ***Windows**: `Documents/Arduino/libraries/USMP`
    *   **macOS/Linux**: `~/Arduino/libraries/USMP`

## Step 2 — Writing Your Sketch

Let's write a simple sketch that joins your Wi-Fi, initiates the secure handshake, sends a packet, and reads incoming responses.

Create a new sketch in your IDE and paste the following code:

```cpp title="basic.ino"
#include <USMP.h>

// Configuration
#define PSK        "usmp-dev-psk-change-me-before-prod"
#define SERVER_IP  "192.168.1.100" // Change this to your Python gateway's IP address
#define WIFI_SSID  "YourNetworkSSID"
#define WIFI_PASS  "YourNetworkPassword"

// Create our secure client with the Pre-Shared Key
USMPClient usmp(PSK);

void setup() {
    Serial.begin(115200);

    // 1. Connect to Wi-Fi, dial the server, and perform the USMP handshake!
    if (!usmp.begin(USMP::TCP(SERVER_IP).wifi(WIFI_SSID, WIFI_PASS))) {
        Serial.println("USMP connection failed! Check your server status and PSK.");
        return;
    }

    Serial.println("Securely connected!");
    Serial.println("Device ID:  " + usmp.deviceId());
    Serial.println("Session ID: " + usmp.sessionId());

    // 2. Send an encrypted message once the tunnel is active
    usmp.send("Hello from Arduino ESP32!");
}

void loop() {
    // 3. Keep the engine running - maintains keepalives & reconnects automatically!
    usmp.maintain(); 

    // 4. Poll for decrypted incoming messages
    if (usmp.available()) {
        String msg = usmp.read();
        Serial.println("Received: " + msg);
    }
}
```

## Step 3 — Run the Python Gateway

To capture the connection, you'll need the gateway running on your laptop. Go to the [Python Quick Start](quickstart-python.md) to launch the receiver server:

```bash
python server.py
```

## Step 4 — Upload and Monitor

1. Select your ESP32 board and serial port in your IDE.
2. Upload the sketch.
3. Open the **Serial Monitor** (set the baud rate to `115200`).

### Expected Serial Monitor Output

```text
[USMP] Connecting to WiFi: YourNetworkSSID
[USMP] WiFi connected — IP: 192.168.1.50
[USMP] TCP Connected
Securely connected!
Device ID:  ab:cd:ef:01:02:03
Session ID: 5f3b7c2a8e9d0a1b2c3d4e5f6a7b8c9d
```

On the Python server console, you should see:

```text
Device connected: ab:cd:ef:01:02:03
Received: Hello from Arduino ESP32!
```

## Receiving Strategies: Polling vs Callbacks

The USMP Arduino library gives you two strategies to handle incoming data. **Pick one and stick to it—do not mix them!**

### Option 1: Polling (Simple / Sequential style)

As shown in the quickstart code, you call `usmp.available()` and `usmp.read()` inside your `loop()`. This is clean for simple telemetry senders where you want to read data directly in a sequential flow.

### Option 2: Callbacks (Asynchronous / Recommended)

Callbacks let you write clean, event-driven applications. You register a callback function during `setup()`, and `usmp.maintain()` will automatically invoke it whenever a new secure message arrives.

Here's how to write a callback-driven sketch:

```cpp title="callbacks_example.ino"
#include <USMP.h>

USMPClient usmp("your-secret-psk");

void onConnect() {
    Serial.println("Connection established!");
    
    // IMPORTANT: Custom keepalive configuration must happen AFTER connection!
    usmp.keepalive(15000); // Send keepalive ping every 15 seconds
}

void onMessage(const uint8_t *data, size_t len) {
    Serial.print("Received message: ");
    Serial.write(data, len);
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    
    // Register our event handlers before we connect
    usmp.onConnect(onConnect);
    usmp.onMessage(onMessage);

    // Connecting automatically fires onConnect() upon completion
    usmp.begin(USMP::TCP("192.168.1.100").wifi("SSID", "PASS"));
}

void loop() {
    // Keep driving the network thread & trigger callbacks
    usmp.maintain();
    delay(10);
}
```

> [!CAUTION]
> **Keepalive Order is Critical!**
> Always configure your keepalive duration (`usmp.keepalive(ms)`) **after** calling `usmp.begin()` (e.g., inside your `onConnect` callback or after `begin` returns). Under the hood, `begin()` executes a `memset` on the client context, resetting the keepalive interval back to the default of 30 seconds.

## Under the Hood

When you execute `usmp.begin()`, USMP starts an automated cryptographic handshake:

1. **Key Exchange**: Generates a temporary X25519 key pair to establish a forward-secret shared secret.
2. **Mutual Authentication**: Cryptographically binds public keys to HMAC-SHA256 proofs using your PSK to prevent MITM key-swapping.
3. **Key Derivation**: Runs HKDF-SHA256 to generate the final symmetric AES-256-GCM session keys.
4. **Watchdog Maintenance**: The `usmp.maintain()` helper handles keepalive checks and schedules automatic reconnects with an exponential backoff if the network drops.
