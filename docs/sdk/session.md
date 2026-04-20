# DXPSession

Represents an established DXP session. Returned by `DXPServer`
to your session handler.

## Properties

```python
session.device_id   # str — device MAC, e.g. "aa:bb:cc:dd:ee:ff"
session.session_id  # str — session ID hex, e.g. "a1b2c3d4"
```

## Methods

### send

```python
await session.send(data: bytes) -> None
```

Encrypts and sends data. Raises on transport error.

### recv

```python
await session.recv() -> bytes
```

Receives and decrypts the next DATA frame.
Automatically responds to PING frames.
Raises `ConnectionClosedError` on BYE.

### ping

```python
await session.ping() -> None
```

Sends an encrypted PING frame. Used for keepalive.

### bye

```python
await session.bye() -> None
```

Sends a BYE frame and closes the connection gracefully.

## Example

```python
@server.on_session
async def handle(session: DXPSession):
    # Echo server
    while True:
        data = await session.recv()
        await session.send(data)
```
