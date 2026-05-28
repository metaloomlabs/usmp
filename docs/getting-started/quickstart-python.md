# Quick Start — Python Gateway

Set up a USMP gateway in Python that accepts connections from ESP32 devices.

## Prerequisites

- Python 3.11+
- `uv` or `pip`

---

## Step 1 — Install the SDK

```bash
pip install usmp-python
```

Or with uv:

```bash
uv add usmp-python
```

---

## Step 2 — Write a server

```python title="server.py"
import asyncio
from usmp import USMPServer, USMPSession

PSK    = b"usmp-dev-psk-change-me-before-prod"
HOST   = "0.0.0.0"
PORT   = 9000

server = USMPServer(host=HOST, port=PORT, psk=PSK)

@server.on_session
async def handle(session: USMPSession):
    print(f"Device connected: {session.device_id}")

    while True:
        data = await session.recv()
        print(f"Received: {data!r}")
        await session.send(b"ACK")

asyncio.run(server.serve())
```

Run it:

```bash
python server.py
```

---

## Step 3 — Connect your ESP32

Flash your ESP32 with the [ESP32 Quick Start](quickstart-esp32.md) code pointing to your machine's IP address. You should see:

```txt
[USMP] Listening on 0.0.0.0:9000
[USMP] TCP connected: ('192.168.1.x', xxxxx)
[USMP] Session established: device=aa:bb:cc:dd:ee:ff session=12345678
Device connected: aa:bb:cc:dd:ee:ff
Received: b'hello from ESP32'
```

---

## Connect as a client (Python → Python)

You can also use USMP from Python to Python — useful for testing:

```python title="client.py"
import asyncio
from usmp import USMPClient

PSK = b"usmp-dev-psk-change-me-before-prod"

async def main():
    client = USMPClient(host="127.0.0.1", port=9000, psk=PSK)
    await client.connect()

    await client.send(b"hello from Python")
    data = await client.recv()
    print(f"Response: {data!r}")

    await client.disconnect()

asyncio.run(main())
```

---

## Multiple devices

`USMPServer` handles multiple concurrent connections automatically:

```python
@server.on_session
async def handle(session: USMPSession):
    # This runs concurrently for each connected device
    device = session.device_id
    print(f"[{device}] connected")

    while True:
        data = await session.recv()
        print(f"[{device}] {data!r}")
```
