# DXPClient

Connects to a DXP server from Python. Useful for testing, CLI tools,
and Python-to-Python DXP communication.

## Constructor

```python
DXPClient(
    host:      str,
    port:      int,
    psk:       bytes,
    device_id: bytes | None = None,
)
```

| Parameter | Description |
|-----------|-------------|
| `host` | Server IP or hostname |
| `port` | Server TCP port |
| `psk` | Pre-shared key |
| `device_id` | 6-byte device ID. Random if not provided |

## Methods

```python
await client.connect()       # Connect and complete handshake
await client.send(data)      # Send encrypted data
await client.recv()          # Receive and decrypt data
await client.ping()          # Send keepalive ping
await client.disconnect()    # Send BYE and close
```

## Properties

```python
client.session_id  # str | None — current session ID
```

## Example

```python
import asyncio
from dxp import DXPClient

async def main():
    client = DXPClient(
        host="192.168.1.100",
        port=9000,
        psk=b"my-secret-psk",
        device_id=b"\x00\x11\x22\x33\x44\x55",
    )

    await client.connect()
    print(f"Connected, session={client.session_id}")

    await client.send(b"hello server")
    response = await client.recv()
    print(f"Response: {response!r}")

    await client.disconnect()

asyncio.run(main())
```
