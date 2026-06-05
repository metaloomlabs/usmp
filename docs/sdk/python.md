# Python SDK

The USMP Python SDK provides an asyncio-based server and client
for building gateways and test tools.

## Installation

```bash
pip install usmp
```

## Architecture

```
usmp/
  USMPServer    ← accepts device connections
  USMPClient    ← connects to a USMP server
  USMPSession   ← represents an established session
  USMPFrame     ← raw frame access
  errors       ← exception hierarchy
```

## Quick example

```python
import asyncio
from usmp import USMPServer, USMPSession

server = USMPServer(host="0.0.0.0", port=9000, psk=b"your-psk")

@server.on_session
async def handle(session: USMPSession):
    print(f"Connected: {session.device_id}")
    while True:
        data = await session.recv()
        print(f"RX: {data!r}")
        await session.send(b"ACK")

asyncio.run(server.serve())
```

## Detailed API

- [USMPServer](server.md) — accept and handle device connections
- [USMPClient](client.md) — connect to a USMP server from Python
- [USMPSession](session.md) — send and receive encrypted frames
