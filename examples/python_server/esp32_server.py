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
            data = await session.recv()  # transparently handles PING/PONG
            print(f"[RX] {data}")
    except ConnectionClosedError:
        print(f"[CLOSED] {session.device_id}")


asyncio.run(server.serve())
