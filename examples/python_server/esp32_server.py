# examples/esp32_server.py
import asyncio
from usmp import USMPServer, USMPSession, ConnectionClosedError

PSK = b"usmp-dev-psk-change-me-before-prod"
HOST = "0.0.0.0"
PORT = 9000

server = USMPServer(host=HOST, port=PORT, psk=PSK)


@server.on_session
async def handle(session: USMPSession):
    print(f"[SESSION] device={session.device_id} session={session.session_id}")
    try:
        while True:
            data = await session.recv()
            text = data.decode().strip()
            try:
                value = int(text)
                result = value * 2
                print(f"[RX] {value} → sending back {result}")
                await session.send(str(result).encode())
            except ValueError:
                print(f"[SKIP] non-numeric: {text!r}")
    except ConnectionClosedError:
        print(f"[CLOSED] {session.device_id}")


asyncio.run(server.serve())
