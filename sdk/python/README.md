# USMP - Unified Secure Multi-transport Protocol

Secure, encrypted communication for ESP32, Arduino, and IoT devices.

USMP sits between raw TCP (no security) and full TLS (too heavy for microcontrollers) - giving any constrained device a fully encrypted, mutually authenticated session in three function calls.

```python
pip install usmp
```

## What it gives you

- **Mutual authentication** - both device and server verify each other via HMAC-SHA256 + PSK
- **Forward secrecy** - X25519 ephemeral key exchange, new keys every session
- **Encryption** - AES-256-GCM, mandatory, no plaintext mode
- **Replay protection** - monotonic sequence numbers

## Quickstart

### Server

```python
import asyncio
from usmp import USMPServer, USMPSession, ConnectionClosedError

server = USMPServer(host="0.0.0.0", port=9000, psk=b"your-psk-here")

@server.on_session
async def handle(session: USMPSession):
    print(f"Device connected: {session.device_id}")
    try:
        while True:
            data = await session.recv()
            print(f"RX: {data}")
            await session.send(b"got it")
    except ConnectionClosedError:
        print(f"Device disconnected: {session.device_id}")

asyncio.run(server.serve())
```

### Client (Python)

```python
import asyncio
from usmp import USMPClient, ConnectionClosedError

async def main():
    client = USMPClient(host="127.0.0.1", port=9000, psk=b"your-psk-here")
    async with client.connect() as session:
        await session.send(b"hello")
        reply = await session.recv()
        print(f"RX: {reply}")

asyncio.run(main())
```

### Client (ESP32 / Arduino)

```cpp
#include <USMP.h>

USMPClient usmp("your-psk-here");

void setup() {
    usmp.begin(USMP::TCP("192.168.1.100").wifi("SSID", "password"));
    usmp.send("hello from esp32");
}

void loop() {
    usmp.maintain(); // keepalive + reconnect

    if (usmp.available()) {
        Serial.println(usmp.read());
    }
}
```

## Protocol overview

```
Device                        Server
  |                              |
  |-- HELLO (device_id, pub_C) -->|
  |<- CHALLENGE (nonce, pub_S) ---|
  |-- HELLO_ACK (HMAC) ---------->|
  |<- SESSION_OK (session_id) ----|
  |                              |
  |== AES-256-GCM encrypted ======|
```

4-step handshake, then every frame is AES-256-GCM encrypted with a session key derived via X25519 + HKDF-SHA256.

## Installation

```
pip install usmp
```

Requires Python 3.11+.

## ESP32 / Arduino library

The Arduino library and ESP-IDF component are available at [github.com/metaloomlabs/usmp](https://github.com/metaloomlabs/usmp).

---

<p align="center">
  <strong>USMP™</strong> • Developed by <strong><a href="https://github.com/metaloomlabs">Metaloom</a></strong><br>
  Copyright &copy; 2026 <strong><a href="https://github.com/winterx64">Akhil B Xavier (winterx64)</a></strong>
</p>
