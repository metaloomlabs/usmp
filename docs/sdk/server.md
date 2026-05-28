# USMPServer

Accepts incoming USMP connections from devices.

## Constructor

```python
USMPServer(
    host: str = "0.0.0.0",
    port: int = 9000,
    psk:  bytes = b"",
    handshake_timeout: float = 10.0,
    session_timeout:   float = 60.0,
)
```

| Parameter | Description |
|-----------|-------------|
| `host` | Interface to bind to. `0.0.0.0` = all interfaces |
| `port` | TCP port to listen on |
| `psk` | Pre-shared key — must match the device |
| `handshake_timeout` | Seconds before a slow handshake is dropped |
| `session_timeout` | Seconds before an idle session is dropped |

## on_session decorator

```python
@server.on_session
async def handle(session: USMPSession) -> None:
    ...
```

Registers an async handler called for each new device connection.
The handler runs concurrently for each connected device.

## serve()

```python
await server.serve()
```

Starts the server and runs forever. Call with `asyncio.run()`:

```python
asyncio.run(server.serve())
```

## Full example

```python
import asyncio
from usmp import USMPServer, USMPSession
from usmp.errors import ConnectionClosedError

PSK = b"my-secret-psk"

server = USMPServer(
    host="0.0.0.0",
    port=9000,
    psk=PSK,
    handshake_timeout=10.0,
    session_timeout=120.0,
)

@server.on_session
async def handle(session: USMPSession):
    print(f"[{session.device_id}] connected, session={session.session_id}")
    try:
        while True:
            data = await session.recv()
            print(f"[{session.device_id}] RX: {data!r}")
            await session.send(b"ACK")
    except ConnectionClosedError:
        print(f"[{session.device_id}] disconnected")

asyncio.run(server.serve())
```
