# USMP | Unified Secure Multi-transport Protocol

> Secure, encrypted device communication for ESP32, Arduino, and IoT. E2E encrypted sessions that run anywhere.

[![Python SDK](https://img.shields.io/pypi/v/usmp?label=usmp&color=blue)](https://pypi.org/project/usmp)
[![Tests](https://img.shields.io/badge/tests-61%20passing-brightgreen)](#testing)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.3%2B-blue)](#esp32-esp-idf)
[![Arduino](https://img.shields.io/badge/Arduino-ESP32-teal)](#arduino)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](#license)

## What is USMP?

USMP fills the gap between **raw TCP (no security)** and **full TLS (too heavy for microcontrollers)**.

It gives any constrained device a fully encrypted, mutually authenticated session with a gateway - with three function calls:

```c
usmp_transport_tcp_init(&transport, "192.168.1.1", 9000);
usmp_connect(&ctx, &transport);
usmp_send(&ctx, data, len);
```

**No insecure mode. Every session has:**

- Mutual authentication - both sides verify each other via HMAC-SHA256 + PSK
- Forward secrecy - X25519 ephemeral key exchange per session
- Encryption - AES-256-GCM, mandatory
- Replay protection - monotonic sequence numbers per session

## Features

- **Transport agnostic** - same protocol over TCP, UART (v0.4.0), BLE (planned)
- **Platform agnostic** - pure C core with 5 platform hooks
- **Reconnect + keepalive** - automatic PING/PONG, explicit reconnect API
- **Python SDK** - asyncio `USMPServer`, `USMPClient`, `USMPSession`
- **ESP32 ready** - ESP-IDF v5+ component, tested on real hardware
- **Arduino ready** - installable `.zip` library, single `#include <USMP.h>`
- **61 tests** - unit, crypto, handshake, integration, API surface

## Supported Platforms

| Platform | Status |
|---|---|
| ESP32 (ESP-IDF v5+) | ✅ Production ready |
| ESP32 (Arduino) | ✅ Production ready |
| Python 3.11+ | ✅ Production ready |
| STM32 | 🔜 Planned |
| Linux | 🔜 Planned |

## Quick Start

### Python Server

```bash
pip install usmp
```

```python
import asyncio
from usmp import USMPServer, USMPSession, ConnectionClosedError

PSK = b"your-psk-here"

server = USMPServer(host="0.0.0.0", port=9000, psk=PSK)

@server.on_session
async def handle(session: USMPSession):
    print(f"Device connected: {session.device_id}")
    try:
        while True:
            data = await session.recv()
            print(f"RX: {data}")
            await session.send(b"ACK")
    except ConnectionClosedError:
        print(f"Device disconnected: {session.device_id}")

asyncio.run(server.serve())
```

### ESP32 - ESP-IDF

```c
#include "usmp.h"
#include "usmp_transport.h"

void app_main(void) {
    wifi_init();

    usmp_t ctx = {0};
    usmp_transport_t transport = {0};

    usmp_transport_tcp_init(&transport, "192.168.1.1", 9000);
    usmp_connect(&ctx, &transport);

    ctx.keepalive_ms = 15000;

    usmp_send(&ctx, (const uint8_t *)"hello", 5);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (usmp_keepalive_tick(&ctx) < 0)
            usmp_reconnect(&ctx);
    }
}
```

### ESP32 - Arduino

```cpp
#include <USMP.h>

#define PSK       "your-psk-here"
#define SERVER_IP "192.168.1.1"
#define WIFI_SSID "YourNetwork"
#define WIFI_PASS "YourPassword"

USMPClient usmp(PSK);

void setup() {
    Serial.begin(115200);

    if (!usmp.begin(USMP::TCP(SERVER_IP).wifi(WIFI_SSID, WIFI_PASS))) {
        Serial.println("Connect failed");
        return;
    }

    Serial.println("Session: " + usmp.sessionId());
    usmp.send("hello from arduino");
}

void loop() {
    usmp.maintain(); // keepalive + reconnect

    if (usmp.available())
        Serial.println("RX: " + usmp.read());
}
```

## Protocol

### Frame Format

```py
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

### Packet Types

| Value | Name | Direction |
|---|---|---|
| `0x01` | HELLO | Device → Server |
| `0x02` | CHALLENGE | Server → Device |
| `0x03` | HELLO_ACK | Device → Server |
| `0x04` | SESSION_OK | Server → Device |
| `0x05` | DATA | Both |
| `0x06` | PING | Both |
| `0x07` | PONG | Both |
| `0x08` | BYE | Both |
| `0xFF` | ERROR | Both |

### Handshake

```cpp
Device                          Server
  |                               |
  |─── HELLO (device_id, pub_C) ─→|
  |                               |
  |←── CHALLENGE (nonce, pub_S) ──|
  |                               |  ← X25519 shared secret computed
  |                               |  ← HKDF session key derived
  |                               |
  |─── HELLO_ACK (HMAC_client) ──→|  ← Client proves PSK knowledge
  |                               |
  |←── SESSION_OK (id, HMAC_srv) ─|  ← Server proves PSK knowledge
  |                               |
  |   [Encrypted session begins]  |
```

**Session key derivation:**

```c
session_key = HKDF-SHA256(
    ikm  = X25519(priv_C, pub_S),
    salt = nonce,
    info = "usmp-v1" || pub_C || pub_S
)
```

## Installation

### Python SDK

```bash
pip install usmp
# or
uv add usmp
```

### ESP32 - ESP-IDF

Add to your `idf_component.yml`:

```yaml
dependencies:
  metaloomlabs/usmp: ">=0.2.0"
```

Or clone and add as a local component:

```bash
cd your_project/components
git clone https://github.com/metaloomlabs/usmp.git
```

### Arduino

1. Download `usmp-arduino.zip` from [Releases](https://github.com/metaloomlabs/usmp/releases)
2. Arduino IDE → Sketch → Include Library → Add .ZIP Library
3. Select the downloaded zip

Or build from source:

```powershell
.\scripts\build-arduino-zip.ps1
```

---

## C API Reference

### Connection

```c
// Initialize TCP transport (allocates context internally)
int usmp_transport_tcp_init(usmp_transport_t *t, const char *ip, int port);

// Connect and perform handshake
int usmp_connect(usmp_t *ctx, usmp_transport_t *transport);

// Explicit reconnect - full new handshake, resets sequence numbers
int usmp_reconnect(usmp_t *ctx);

// Close session
void usmp_close(usmp_t *ctx);

// Check connection state
bool usmp_is_connected(const usmp_t *ctx);
```

### Data

```c
// Send encrypted data (max USMP_MAX_DATA_LEN = 464 bytes)
int usmp_send(usmp_t *ctx, const uint8_t *data, uint16_t len);

// Receive and decrypt data
int usmp_recv(usmp_t *ctx, uint8_t *out, uint16_t max_len);
```

### Keepalive

```c
// Send PING frame
int usmp_ping(usmp_t *ctx);

// Call in main loop - sends PING if keepalive_ms elapsed since last TX
// Returns -1 if connection dead (time to call usmp_reconnect)
int usmp_keepalive_tick(usmp_t *ctx);
```

### Platform hooks (implement for your target)

```c
int      usmp_port_get_device_id(uint8_t *out, size_t len);
int      usmp_port_random(uint8_t *out, size_t len);
void     usmp_port_delay_ms(uint32_t ms);
uint32_t usmp_port_millis(void);
void     usmp_port_log(char level, const char *tag, const char *msg);
```

## Python SDK Reference

### USMPServer

```python
server = USMPServer(
    host="0.0.0.0",
    port=9000,
    psk=b"your-psk",
    handshake_timeout=10.0,    # seconds
    session_timeout=60.0,      # seconds - watchdog fires if no data/PING
    on_timeout=my_callback,    # async fn(device_id, session_id) - optional
)

@server.on_session
async def handler(session: USMPSession):
    data = await session.recv()   # transparently handles PING/PONG
    await session.send(b"reply")

asyncio.run(server.serve())
```

### USMPClient

```python
client = USMPClient(
    host="192.168.1.1",
    port=9000,
    psk=b"your-psk",
    device_id=bytes(6),    # optional - auto-generated if not provided
)

await client.connect()
await client.send(b"hello")
data = await client.recv()
await client.ping()
await client.disconnect()
```

### USMPSession

```python
# Available inside @server.on_session handler
session.device_id    # "aa:bb:cc:dd:ee:ff"
session.session_id   # "a1b2c3d4"

await session.send(b"data")
data = await session.recv()   # blocks until DATA frame (skips PING/PONG)
await session.ping()
await session.bye()
```

### Arduino API

```cpp
USMPClient usmp(PSK);

// Connect
usmp.begin(USMP::TCP("ip", port).wifi("ssid", "pass"));

// Send
usmp.send("string");
usmp.send(buf, len);

// Receive
if (usmp.available())
    String msg = usmp.read();

// State
usmp.alive()       // bool
usmp.deviceId()    // String "aa:bb:cc:dd:ee:ff"
usmp.sessionId()   // String "a1b2c3d4"

// Keepalive config
usmp.keepalive(15000);  // PING every 15s

// Main loop driver - handles PING + reconnect + onMessage callback
usmp.maintain();

// Callbacks
usmp.onConnect(fn);
usmp.onDisconnect(fn);
usmp.onReconnect(fn);
usmp.onMessage(fn);  // fn(const uint8_t *data, size_t len)
```

## Testing

```bash
cd sdk/python
uv run pytest tests/ -v
```

```c
61 passed in 3.3s
├── 16 API surface tests
├── 8  benchmark tests
├── 8  crypto tests
├── 7  frame tests
├── 12 handshake tests
├── 8  integration tests (real loopback TCP)
└── 3  session tests
```

## Repository Structure

```txt
usmp/
  core/                    ← Pure C, zero platform dependencies
    include/               ← Public headers
    src/                   ← Implementation

  ports/
    usmp-esp32/            ← ESP-IDF v5+ port
    usmp-arduino/          ← Arduino library

  sdk/
    python/                ← Python asyncio SDK
      src/usmp/
      tests/

  scripts/
    build-arduino-zip.ps1  ← Build Arduino .zip (Windows)
    build-arduino-zip.sh   ← Build Arduino .zip (macOS/Linux)
    publish-pypi.ps1
    publish-pypi.sh
    test-sdk.ps1
    test-sdk.sh

  firmware/                ← ESP32 reference firmware (ESP-IDF)
  examples/                ← Usage examples
  docs/                    ← MkDocs Material documentation
```

## Roadmap

```python
v0.2.0 ✅  Core protocol + Reconnect + Keepalive + Arduino port
v0.3.0 🔨  Publish
             - Python SDK on PyPI
             - ESP-IDF component registry
             - PlatformIO library ✅
v0.4.0 📋  UART transport
             - COBS framing
             - ACK/retry layer
v0.5.0 📋  Discovery + CLI
             - mDNS + UDP broadcast scan
             - usmp scan / connect / logs / send / monitor
v0.6.0 📋  OTA firmware update
             - Ed25519 signed firmware
             - Chunked transfer + atomic swap
v0.7.0 📋  Multi-device hardening
             - Per-device PSK
             - NVS key storage
             - Device-to-device (usmp_listen on ESP32)
v1.0.0 📋  Cloud bridge
             - Gateway → MQTT / WebSocket
             - Remote CLI via cloud
```

## Security

**Development PSK:** The default PSK `usmp-dev-psk-change-me-before-prod` is for development only. Always use a strong, secret PSK in production.

**Threat model:** USMP protects against passive eavesdropping, active MITM, replay attacks, and rogue server/device attacks. It does not protect against physical compromise of the device or PSK exposure.

## Contributing

1. Fork the repo
2. Create a feature branch
3. Run tests: `uv run pytest tests/ -v`
4. Build Arduino zip: `.\scripts\build-arduino-zip.ps1`
5. Build ESP-IDF: `idf.py build`
6. Submit a PR

## License

Apache License 2.0 - see [LICENSE](LICENSE)

## Author

**winterx64** - [github.com/winterx64](https://github.com/winterx64)

---

<p align="center">
  <strong>USMP™</strong> • Developed by <strong><a href="https://github.com/metaloomlabs">Metaloom</a></strong><br>
  Copyright &copy; 2026 <strong><a href="https://github.com/winterx64">Akhil B Xavier (winterx64)</a></strong>
</p>

