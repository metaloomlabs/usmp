# USMP — Arduino Library for ESP32

> ⚠️ **Note:** This repository is a read-only distribution mirror of the USMP monorepo.
> All development, pull requests, and issues should be submitted to [metaloomlabs/usmp](https://github.com/metaloomlabs/usmp).

Secure, lightweight, encrypted device communication protocol for ESP32 on the Arduino framework.

USMP sits between raw sockets (no security) and full TLS/DTLS (too heavy for microcontrollers) — providing an AES-256-GCM encrypted, mutually authenticated session with ephemeral key exchange in just three function calls.

## Features

- **Mutual Authentication**: HMAC-SHA256 verification using a pre-shared key (PSK).
- **Forward Secrecy**: X25519 ephemeral key exchange generated per session.
- **Mandatory Encryption**: AES-256-GCM encryption with replay protection.
- **Transports**: Built-in support for TCP and UDP connections.
- **Low Footprint**: Optimized C protocol engine with zero heavy TLS overhead.

## Installation

### Via Arduino IDE Library Manager
Search for **USMP** in the Arduino IDE Library Manager (**Sketch** ➔ **Include Library** ➔ **Manage Libraries...**) and click **Install**.

### Via ZIP Archive
Download the packaged release archive `usmp-<version>-arduino.zip` and import it in Arduino IDE via **Sketch** ➔ **Include Library** ➔ **Add .ZIP Library...**.

## Quickstart

```cpp
#include <USMP.h>

// WARNING: Provision PSK securely in production (e.g. NVS / flash partition)
#define PSK "your-pre-shared-key"
#define SERVER_IP "192.168.1.100"
#define WIFI_SSID "YourNetwork"
#define WIFI_PASS "YourPassword"

USMPClient usmp(PSK);

void setup() {
  Serial.begin(115200);

  // Connect WiFi, configure transport (TCP or UDP), and perform handshake
  auto transport = USMP::TCP(SERVER_IP).wifi(WIFI_SSID, WIFI_PASS);

  if (!usmp.begin(transport)) {
    Serial.println("USMP connect failed — check server IP and PSK");
    return;
  }

  Serial.println("USMP session established!");
  usmp.send("Hello from Arduino ESP32!");
}

void loop() {
  // Maintain session keepalive and auto-reconnect
  usmp.maintain();

  if (usmp.available()) {
    String message = usmp.read();
    Serial.println("Received: " + message);
  }
}
```

## Supported Architectures

- ESP32 (`esp32`, `esp32s2`, `esp32s3`, `esp32c3`, `esp32c6`)

## License

Apache-2.0
