# USMP - Unified Secure Multi-transport Protocol

Secure, encrypted communication for ESP32, Arduino, and IoT devices.

USMP sits between raw sockets (no security) and full TLS/DTLS (too heavy for microcontrollers) - giving any constrained device a fully encrypted, mutually authenticated session in three function calls.

```bash
pip install usmp
```

## What it gives you

- **Mutual authentication** - both device and server verify each other via HMAC-SHA256 + PSK
- **Forward secrecy** - X25519 ephemeral key exchange, new keys every session
- **Encryption** - AES-256-GCM, mandatory, no plaintext mode
- **Replay protection** - monotonic sequence numbers
- **Multiple Transports** - Production-ready support for both TCP and UDP streams.

## Quickstart

### Server

```python
import asyncio
from usmp import USMPServer, USMPSession, USMPProtocol, ConnectionClosedError

# Initialize server
server = USMPServer(host="0.0.0.0", port=9000, psk=b"your-psk-here", protocol=USMPProtocol.TCP)

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

async def main():
    await server.serve()

if __name__ == "__main__":
    asyncio.run(main())
```

### Client (Python)

```python
import asyncio
from usmp import USMPClient, USMPProtocol

async def main():
    # Initialize client
    client = USMPClient(host="127.0.0.1", port=9000, psk=b"your-psk-here", protocol=USMPProtocol.TCP)
    await client.connect()
    
    await client.send(b"hello")
    reply = await client.recv()
    print(f"RX: {reply}")
    
    await client.disconnect()

if __name__ == "__main__":
    asyncio.run(main())
```

### Client (Arduino ESP32)

```cpp
#include <USMP.h>

USMPClient usmp("your-psk-here");

void setup() {
    // Connect using TCP or UDP
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

```bash
pip install usmp
```

Requires Python 3.11+.

## ESP32 / Arduino library

The Arduino library and ESP-IDF component are available directly through their respective package registries:

- **ESP-IDF Component**: Add as a dependency by running `idf.py add-dependency "metaloomlabs/usmp"` in your project directory.
- **Arduino Library**: Import the packaged release ZIP archive `usmp-1.0.1-arduino.zip` via **Sketch** ➔ **Include Library** ➔ **Add .ZIP Library...**

<p align="center">
  <strong>USMP™</strong> • Developed by <strong><a href="https://github.com/metaloomlabs">Metaloom</a></strong>
</p>
