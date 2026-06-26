# USMP | Unified Secure Multi-transport Protocol

> **Say goodbye to the "IoT Security Gap"!** USMP is a lightweight, secure, and developer-friendly protocol designed to bring end-to-end encrypted, mutually authenticated sessions to ESP32, Arduino, and Python.

[![Python SDK](https://img.shields.io/pypi/v/usmp?label=usmp&color=blue)](https://pypi.org/project/usmp)
[![Tests](https://img.shields.io/badge/tests-69%20passing-brightgreen)](#testing)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.3%2B-blue)](#esp32-esp-idf)
[![Arduino](https://img.shields.io/badge/Arduino-ESP32-teal)](#arduino)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](#license)

## Why USMP?

In the world of IoT, developers are often forced to make a frustrating choice when connecting devices:
1. **Raw TCP / Serial / BLE**: Fast and lightweight, but completely open to eavesdropping and tampering.
2. **Full TLS**: Rock-solid, but extremely heavy, slow to shake hands, and often resource-prohibitive for smaller microcontrollers.

**USMP fills this gap.** It gives you a lightweight, transport-agnostic, and secure tunnel that runs anywhere. Establishing a secure connection on your device is as simple as:

```c
// 1. Initialize your transport (TCP, UART, etc.)
usmp_transport_tcp_init(&transport, "192.168.1.1", 9000);

// 2. Perform the handshake and establish a secure session
usmp_connect(&ctx, &transport);

// 3. Send encrypted data safely!
usmp_send(&ctx, data, len);
```

### Iron-clad Security, Built-In
USMP doesn't do "insecure mode." Every single session gets:
*   **Mutual Authentication**: Both sides verify each other's identity using HMAC-SHA256 and a Pre-Shared Key (PSK).
*   **Forward Secrecy**: An ephemeral X25519 key exchange occurs with every session, ensuring that past traffic remains secret even if keys are compromised later.
*   **Mandatory Encryption**: All payload data is encrypted using AES-256-GCM.
*   **Tamper Resistance**: Ephemeral public keys (`pub_C` and `pub_S`) are cryptographically bound directly to the handshake HMACs, preventing Man-in-the-Middle (MITM) key-swapping attacks.
*   **Replay Protection**: Strict, monotonic 32-bit sequence numbers are verified for every frame.
*   **Deterministic Nonces**: Under the hood, AES-GCM nonces are deterministically constructed as `seq (4 bytes, Little-Endian) || session_id[0..7]` to eliminate nonce collision risks.

## Key Features

*   **Transport Agnostic**: Runs over TCP, UART (with robust COBS framing & sliding window ACKs), and soon BLE!
*   **Platform Agnostic**: A pure C core with only 5 platform hooks to implement for any new platform.
*   **Dynamic Payload Fragmentation**: Need to send large payloads? USMP dynamically fragments payloads up to ~1.8 KB into multiple frames (plaintext capacity of 452 bytes per frame) and transparently reassembles them at the receiver.
*   **Built-in Rate Limiting & Hardening**: The Python server automatically protects itself by locking out offending clients/IPs for up to 60 seconds after consecutive handshake failures.
*   **Plug & Play SDKs**:
    *   **Python**: Fully async, event-driven SDK with a server and client.
    *   **ESP-IDF**: Ready-to-go component for ESP32 devices.
    *   **Arduino**: Single-header `#include <USMP.h>` with intuitive callback interfaces.

## Supported Platforms

| Platform | Status | Getting Started |
|:---|:---|:---|
| **ESP32 (ESP-IDF v5+)** | Production ready | [ESP-IDF Quickstart](docs/getting-started/quickstart-esp32.md) |
| **ESP32 (Arduino)** | Production ready | [Arduino Quickstart](docs/getting-started/quickstart-arduino.md) |
| **Python 3.11+** | Production ready | [Python Quickstart](docs/getting-started/quickstart-python.md) |
| **STM32** | Planned | [Porting Guide](docs/ports/porting-guide.md) |
| **Linux** | Planned | - |

## Quick Start

Here is how you can set up a secure ecosystem in minutes.

### Python Server
Run `pip install usmp` and launch this async server:

```python
import asyncio
from usmp import USMPServer, USMPSession, ConnectionClosedError

# Keep this secret!
PSK = b"your-psk-here-make-it-long-and-secure"

server = USMPServer(host="0.0.0.0", port=9000, psk=PSK)

@server.on_session
async def handle_device(session: USMPSession):
    print(f"Device connected: {session.device_id}")
    try:
        while True:
            # Receive decrypted data
            data = await session.recv()
            print(f"Received: {data.decode('utf-8')}")
            
            # Send an encrypted reply
            await session.send(b"Got your message loud and clear!")
    except ConnectionClosedError:
        print(f"Device disconnected: {session.device_id}")

print("USMP Server starting on port 9000...")
async def main():
    await server.serve()

if __name__ == "__main__":
    asyncio.run(main())
```

### ESP32 - ESP-IDF (C)
Initialize your network, hook up USMP, and send secure telemetry:

```c
#include "usmp.h"
#include "usmp_transport.h"

void app_main(void) {
    // 1. Set up your Wi-Fi or Ethernet
    wifi_init();

    usmp_t ctx = {0};
    usmp_transport_t transport = {0};

    // 2. Configure the TCP transport & connect
    usmp_transport_tcp_init(&transport, "192.168.1.1", 9000);
    if (usmp_connect(&ctx, &transport) == 0) {
        printf("Secure session established!\n");
    }

    // 3. Configure keepalives (e.g., 15 seconds)
    ctx.keepalive_ms = 15000;

    // 4. Send encrypted messages
    usmp_send(&ctx, (const uint8_t *)"Hello, USMP!", 12);

    // 5. Maintain the session in your loop
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Tick keepalive and handle automatic reconnects if the link drops
        if (usmp_keepalive_tick(&ctx) < 0) {
            printf("Connection lost, reconnecting...\n");
            usmp_reconnect(&ctx);
        }
    }
}
```

### ESP32 - Arduino (C++)
Use the friendly C++ wrapper with event-driven callbacks:

```cpp
#include <USMP.h>

#define PSK       "your-psk-here-make-it-long-and-secure"
#define SERVER_IP "192.168.1.1"
#define WIFI_SSID "YourNetwork"
#define WIFI_PASS "YourPassword"

USMPClient usmp(PSK);

// Callback triggered when we get an encrypted message
void onMessageReceived(const uint8_t *data, size_t len) {
    Serial.print("Received Message: ");
    Serial.write(data, len);
    Serial.println();
}

void setup() {
    Serial.begin(115200);

    // Register our callback
    usmp.onMessage(onMessageReceived);

    // Start connection (handles Wi-Fi and Handshake automatically!)
    if (!usmp.begin(USMP::TCP(SERVER_IP).wifi(WIFI_SSID, WIFI_PASS))) {
        Serial.println("Connection failed!");
        return;
    }

    // IMPORTANT: Keepalive configuration must be set AFTER begin()!
    usmp.keepalive(15000);

    Serial.println("Session started! ID: " + usmp.sessionId());
    usmp.send("Hello from Arduino ESP32!");
}

void loop() {
    // Keep the engine running - maintains keepalives & reconnects under the hood
    usmp.maintain();
}
```

## Protocol Overview

### Frame Format
Each USMP packet is packed tightly to minimize overhead on constrained networks:

```text
 0               1               2               3
 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     Magic (0xABCD)            | Version       | Type          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Sequence Number (32-bit)                   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     Payload Length            |     CRC-16/IBM                |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     Payload (max 480 bytes, encrypted with AES-256-GCM)       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

*   **Magic (16 bits)**: Always `0xABCD` to verify frame alignment.
*   **Version (8 bits)**: Current version (e.g. `0x01`).
*   **Type (8 bits)**: Packet function (e.g., `0x01` HELLO, `0x05` DATA, `0x09` DATA_FRAG).
*   **Sequence Number (32 bits)**: Strict counter preventing replays.
*   **Payload Length (16 bits)**: Size of the payload (up to 480 bytes including AES-GCM tag).
*   **CRC-16 (16 bits)**: Error detection over the header and payload.

For a deeper dive into the wire layout, see [Frame Format Details](docs/protocol/frame-format.md).

### The Handshake Sequence
Here is how USMP establishes trust, negotiates keys, and binds them cryptographically:

```text
Device                          Server
  |                               |
  |─── HELLO (device_id, pub_C) ─→|
  |                               |
  |←── CHALLENGE (nonce, pub_S) ──|
  |                               |  ← Compute X25519 shared secret
  |                               |  ← Derive session keys using HKDF-SHA256
  |                               |
  |─── HELLO_ACK (HMAC_client) ──→|  ← Prove knowledge of PSK & bind pub_C/pub_S
  |                               |
  |←── SESSION_OK (id, HMAC_srv) ─|  ← Server confirms PSK knowledge & key binding
  |                               |
  |   [Encrypted Session Open]    |
```

*For more details on key binding and session derivation, check out the [Handshake Specification](docs/protocol/handshake.md).*

## Testing

We love reliability. USMP is backed by a comprehensive suite of unit, integration, and performance tests:

```bash
cd sdk/python
uv run pytest tests/ -v
```

```text
======================= 69 passed in 3.42s =======================
├── 16 API surface tests
├── 8  benchmark tests
├── 8  crypto tests
├── 7  frame tests
├── 12 handshake tests
├── 8  integration tests (real loopback TCP)
└── 10 session & fragmentation tests
```

## Repository Structure

*   `core/`: Pure C implementation of the USMP protocol state machine. Zero heap allocations, zero platform dependencies.
*   `ports/`: Platform-specific adaptors and client SDKs.
    *   `usmp-esp32/`: Native ESP-IDF v5 component.
    *   `usmp-arduino/`: Arduino library.
*   `sdk/python/`: Fully async Python SDK for servers and test clients.
*   `examples/`: Sample code to get you up and running quickly.
*   `docs/`: Full documentation site (written in friendly Markdown).

## Roadmap & Ecosystem

*   [x] **v0.2.0**: Core protocol, Keepalive mechanism, and Arduino Port.
*   [x] **v0.3.0**: Python SDK published on PyPI.
*   [x] **v0.4.0**: UART Transport layer with COBS framing & sliding window.
*   [x] **v0.4.7**: Security hardening (Key Binding, Deterministic Nonces, Rate Limiting, Dynamic Fragmentation).
*   [ ] **v0.5.0**: CLI tools and auto-discovery (mDNS / UDP).
*   [ ] **v0.6.0**: Secure OTA firmware updates with Ed25519 signatures.

## Contributing

We welcome all contributions! To get started:
1. Fork the repository.
2. Create a feature branch (`git checkout -b feature/cool-new-thing`).
3. Ensure all tests pass: `uv run pytest tests/ -v`.
4. Open a Pull Request!

Please review our [Contributing Guidelines](CONTRIBUTING.md) and [Code of Conduct](LICENSE) before submitting.

<p align="center">
  <strong>USMP™</strong> • Crafted by <strong><a href="https://github.com/metaloomlabs">Metaloom</a></strong><br>
  Copyright &copy; 2026 <strong><a href="https://github.com/winterx64">Akhil B Xavier (winterx64)</a></strong>
</p>
