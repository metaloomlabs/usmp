# Python SDK

The DXP Python SDK provides an asyncio-based server and client
for building gateways and test tools.

## Installation

```bash
pip install dxp-python
```

## Architecture

```
dxp/
  DXPServer    ← accepts device connections
  DXPClient    ← connects to a DXP server
  DXPSession   ← represents an established session
  DXPFrame     ← raw frame access
  errors       ← exception hierarchy
```

## Quick example

```python
import asyncio
from dxp import DXPServer, DXPSession

server = DXPServer(host="0.0.0.0", port=9000, psk=b"your-psk")

@server.on_session
async def handle(session: DXPSession):
    print(f"Connected: {session.device_id}")
    while True:
        data = await session.recv()
        print(f"RX: {data!r}")
        await session.send(b"ACK")

asyncio.run(server.serve())
```

## Detailed API

- [DXPServer](server.md) — accept and handle device connections
- [DXPClient](client.md) — connect to a DXP server from Python
- [DXPSession](session.md) — send and receive encrypted frames
