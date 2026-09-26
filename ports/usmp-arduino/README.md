# USMP — Arduino Library for ESP32

> ⚠️ **Note:** This repository is a read-only distribution mirror of the USMP monorepo.
> All development, pull requests, and issues should be submitted to [metaloomlabs/usmp](https://github.com/metaloomlabs/usmp).

Secure, lightweight, encrypted device communication protocol for ESP32 on the Arduino framework.

USMP sits between raw sockets (no security) and full TLS/DTLS (too heavy for microcontrollers) — providing an AES-256-GCM / ChaCha20-Poly1305 encrypted, mutually authenticated session with ephemeral key exchange in just three function calls.

## Features

- **Mutual Authentication**: HMAC-SHA256 verification using a pre-shared key (PSK).
- **Forward Secrecy**: X25519 ephemeral key exchange generated per session.
- **Mandatory Encryption**: AES-256-GCM / ChaCha20-Poly1305 AEAD encryption with replay protection.
- **Standard `Print` Interface**: Inherits from Arduino's `Print` class (`usmp.print()`, `usmp.println()`) with zero-heap line buffering.
- **Zero-Heap Execution**: `USMPClientStatic` template wrapper and static transport contexts for 100% `.bss` static allocation.
- **Asynchronous Handshake FSM**: Non-blocking `beginAsync()` prevents CPU stalls and watchdog resets.
- **Descriptive Error Codes**: `usmp.lastError()` and `usmp.lastErrorString()` for instant debugging.
- **Transports**: Built-in support for TCP and UDP (with CoAP-style RTT estimation and retransmission).
- **Low Footprint**: Optimized C protocol engine with zero heavy TLS overhead.

## Installation

### Via Arduino IDE Library Manager

Search for **USMP** in the Arduino IDE Library Manager (**Sketch** ➔ **Include Library** ➔ **Manage Libraries...**) and click **Install**.

### Via ZIP Archive

Download the packaged release archive `usmp-<version>-arduino.zip` and import it in Arduino IDE via **Sketch** ➔ **Include Library** ➔ **Add .ZIP Library...**.

---

## 5-Minute Quickstart

### Step 1: Start the Python Test Server

In your computer's terminal:

```bash
pip install usmp
python -m usmp.server --echo --port 9000 --psk "your-secret-key-16b"
```

### Step 2: Upload Arduino Sketch

```cpp
#include <USMP.h>

#define SERVER_IP "192.168.1.100"  // IP of your computer running usmp.server
#define WIFI_SSID "YourNetwork"
#define WIFI_PASS "YourPassword"

// Binary or string pre-shared key (must match server PSK, min 16 bytes)
static const uint8_t PSK[] = "your-secret-key-16b";
USMPClient usmp(PSK, sizeof(PSK) - 1);

void setup() {
  Serial.begin(115200);

  // Connect WiFi, configure transport (TCP or UDP), and perform handshake
  auto transport = USMP::TCP(SERVER_IP).wifi(WIFI_SSID, WIFI_PASS);

  if (!usmp.begin(transport)) {
    Serial.printf("USMP connect failed: %s (code %d)\n",
                  usmp.lastErrorString(), (int)usmp.lastError());
    return;
  }

  Serial.println("USMP session established!");

  // Standard Arduino Print interface (line-buffered, zero heap!)
  usmp.print("Telemetry: temp=");
  usmp.print(24.5);
  usmp.println("C status=OK");
}

void loop() {
  // Maintain session keepalive and auto-reconnect
  usmp.maintain();

  if (usmp.available()) {
    String message = usmp.read();
    Serial.println("Received from server: " + message);
  }
}
```

---

## Developer Experience (DX) Highlights

### 1. Standard Arduino `Print` Inheritance

`USMPClient` inherits from Arduino's core `Print` class. Characters are buffered in an embedded zero-heap array and sent as a single encrypted frame whenever `\n` is encountered or `flush()` is called:

```cpp
usmp.print("Sensor reading: ");
usmp.print(analogRead(A0));
usmp.println(" mV"); // Triggers transmission of single encrypted frame!
```

### 2. Descriptive Error Codes

Inspect failed connections or sends without guessing:

```cpp
if (!usmp.begin(transport)) {
  Serial.printf("Failed: %s (%d)\n", usmp.lastErrorString(), usmp.lastError());
  // Prints: "Failed: Authentication failed / invalid PSK (-6)"
}
```

### 3. Non-Blocking Handshakes (`beginAsync`)

For applications with active sensors or displays that cannot afford a synchronous 2-5 second blocking connection stall:

```cpp
usmp.beginAsync(transport);

void loop() {
  usmp.maintain(); // Advances connection FSM non-blocking

  if (usmp.connected()) {
    // Session is established!
  } else if (usmp.isConnecting()) {
    // Animate spinner or read sensors...
  }
}
```

### 4. Zero-Heap Static Allocation (`USMPClientStatic`)

For zero-heap embedded deployments without dynamic `malloc`:

```cpp
#include <USMP.h>

// Embeds 1024-byte RX buffer and 1024-byte scratchpad in .bss
USMPClientStatic<1024, 1024> usmp(PSK, sizeof(PSK) - 1);
static USMPArduinoTcpCtx s_tcp_ctx;

void setup() {
  auto transport = USMP::TCP(SERVER_IP).wifi(WIFI_SSID, WIFI_PASS);
  usmp.begin(transport, &s_tcp_ctx); // 100% zero-heap!
}
```

---

## Supported Architectures

- ESP32 (`esp32`, `esp32s2`, `esp32s3`, `esp32c3`, `esp32c6`)

## License

Apache-2.0
