# USMP — Unified Secure Multi-transport Protocol (Python SDK)

[![PyPI version](https://img.shields.io/pypi/v/usmp?color=blue&label=pypi)](https://pypi.org/project/usmp)
[![Python Versions](https://img.shields.io/pypi/pyversions/usmp)](https://pypi.org/project/usmp)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](https://github.com/metaloomlabs/usmp/blob/main/LICENSE)

> **Say goodbye to the "IoT Security Gap"!** USMP is a lightweight, secure, and developer-friendly session protocol designed to bring end-to-end encrypted, mutually authenticated tunnels to ESP32, Arduino, and Python.

USMP sits between raw sockets (no security) and full TLS/DTLS (too heavy and resource-intensive for microcontrollers) — giving any constrained device a hardened, encrypted session with forward secrecy in just three function calls.

```bash
pip install usmp
```

---

## Key Capabilities

- **Mutual Authentication**: Both device and server verify identity using HMAC-SHA256 and a Pre-Shared Key (PSK).
- **Perfect Forward Secrecy**: Ephemeral X25519 key exchange generates a fresh session key for every connection.
- **Mandatory AEAD Encryption**: AES-256-GCM or ChaCha20-Poly1305. No unencrypted fallback modes.
- **Multi-Transport**: Production-ready support for both **TCP** and **UDP** (with CoAP-style RTT estimation and loss recovery).
- **Built-in CLI Dev / Echo Server**: Instant zero-code test server with ANSI colored badges and diagnostic payload inspection.
- **Developer-Friendly Ergonomics**:
  - `session.print(data)` — Terminal payload inspector (auto UTF-8 vs hex dump with device ID badge and timestamps).
  - `session.recv_str()` — Direct UTF-8 string reception with unicode fallback handling.
  - `session.send(str | bytes)` — Native polymorphic payload transmission.
  - `session.is_connected` & `session.info` — Clean session state inspection.
- **Dynamic Fragmentation**: Transparently fragments and reassembles payloads up to ~1.8 KB across 452-byte frames.
- **Replay Protection & Hardening**: Monotonic 32-bit sequence numbers, deterministic nonces (`seq || session_id[0..7]`), and automatic 60-second brute-force lockout.

---

## Quickstart

### 1. Instant CLI Dev / Echo Server (Zero-Code)

Test your Arduino or ESP32 hardware immediately without writing Python server code:

```bash
# Launch instant echo server with colored ANSI logs and payload inspection:
usmp-server --echo --port 9000 --psk "usmp-dev-psk-change-me-before-prod"

# Or generate a cryptographically secure 32-byte hex PSK:
usmp-server --generate-psk
```

> **Note**: You can also invoke the server via Python module execution:  
> `python -m usmp.server --echo --port 9000 --psk "..."`

### 2. Embedded Async Python Server

Embed a custom high-performance server in your application using `asyncio`:

```python
import asyncio
from usmp import USMPServer, USMPSession, USMPProtocol, ConnectionClosedError

PSK = b"usmp-dev-psk-change-me-before-prod"

server = USMPServer(host="0.0.0.0", port=9000, psk=PSK, protocol=USMPProtocol.TCP)

@server.on_session
async def handle_device(session: USMPSession):
    print(f"Device connected: {session.device_id} ({session.info})")
    try:
        while session.is_connected:
            # Direct string or raw binary reception
            text = await session.recv_str()
            
            # Built-in terminal payload inspector with ANSI badges & timestamps
            session.print(text)
            
            # Send encrypted reply (accepts str or bytes natively)
            await session.send(f"Echo: {text}")
    except ConnectionClosedError:
        print(f"Device disconnected: {session.device_id}")

async def main():
    print("Starting USMP server on port 9000...")
    await server.serve()

if __name__ == "__main__":
    asyncio.run(main())
```

### 3. Python Async Client

Connect to a remote USMP gateway from another Python service or test script:

```python
import asyncio
from usmp import USMPClient, USMPProtocol

async def main():
    client = USMPClient(
        host="127.0.0.1",
        port=9000,
        psk=b"usmp-dev-psk-change-me-before-prod",
        protocol=USMPProtocol.TCP,
    )
    await client.connect()
    
    # Send string or bytes natively
    await client.send("Hello from Python client!")
    
    # Receive response
    reply = await client.recv_str(timeout=5.0)
    print(f"Server replied: {reply}")
    
    await client.disconnect()

if __name__ == "__main__":
    asyncio.run(main())
```

### 4. ESP32 Arduino Client (C++)

Pair your Python server with an ESP32 running Arduino:

```cpp
#include <USMP.h>

#define SERVER_IP "192.168.1.100"
#define WIFI_SSID "YourNetwork"
#define WIFI_PASS "YourPassword"

static const uint8_t PSK[] = "usmp-dev-psk-change-me-before-prod";
USMPClient usmp(PSK, sizeof(PSK) - 1);

void setup() {
  Serial.begin(115200);

  // Connect WiFi and establish encrypted session (TCP or UDP)
  auto transport = USMP::TCP(SERVER_IP).wifi(WIFI_SSID, WIFI_PASS);
  if (!usmp.begin(transport)) {
    Serial.printf("Connection failed: %s (error %d)\n",
                  usmp.lastErrorString(), usmp.lastError());
    return;
  }

  // Idiomatic Arduino Print interface (zero-heap line buffering!)
  usmp.print("Telemetry: temp=");
  usmp.print(24.5);
  usmp.println("C status=OK");
}

void loop() {
  usmp.maintain(); // Keeps session alive and auto-reconnects in background

  if (usmp.available()) {
    String msg = usmp.read();
    Serial.println("Received: " + msg);
  }
}
```

---

## Terminal Payload Inspector (`session.print`)

The Python SDK includes a built-in terminal inspector tailored for IoT debugging:

```python
raw = await session.recv()
session.print(raw)
```

* **UTF-8 Payloads**: Formats as a clean timestamped message with the device ID badge:
  ```text
  [14:23:05] [DE:AD:BE:EF:00:01] Telemetry: temp=24.5C status=OK
  ```
* **Binary Payloads**: Automatically falls back to an organized hex dump with length header:
  ```text
  [14:23:05] [DE:AD:BE:EF:00:01] BINARY (8 bytes):
    0000: 01 02 a3 f4 00 00 ff 10
  ```

---

## Cryptographic Handshake

```text
Device                               Server
  │                                     │
  │── HELLO (device_id, pub_C) ────────►│
  │◄─ CHALLENGE (nonce, pub_S) ─────────│
  │── HELLO_ACK (HMAC-SHA256) ─────────►│
  │◄─ SESSION_OK (session_id) ──────────│
         ↓  Session Key Derived  ↓
  │════ DATA (AES-256-GCM / Poly1305) ═►│
  │◄═══ DATA (AES-256-GCM / Poly1305) ══│
```

Session keys are derived via:
```text
HKDF-SHA256(
    ikm  = X25519(priv_C, pub_S),
    salt = nonce,
    info = "usmp-v1" || pub_C || pub_S
)
```

---

## Ecosystem & Ports

- **ESP-IDF Component (ESP32)**: Available on [ESP Component Registry](https://components.espressif.com/components/metaloomlabs/usmp) (`idf.py add-dependency "metaloomlabs/usmp"`).
- **Arduino Library (ESP32)**: Available via [Arduino Library Manager](https://github.com/metaloomlabs/usmp/tree/main/ports/usmp-arduino) or packaged release ZIP (`usmp-1.3.0-arduino.zip`).
- **Core C Engine**: Zero-dependency portable C core supporting custom microcontroller targets (STM32, Linux, RTOS).

---

## License

USMP is open-source software licensed under the [Apache 2.0 License](https://github.com/metaloomlabs/usmp/blob/main/LICENSE).

<p align="center">
  <strong>USMP™</strong> • Developed by <strong><a href="https://github.com/metaloomlabs">Metaloom</a></strong>
</p>
