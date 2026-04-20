# examples/esp32_server.py
import asyncio
from dxp import DXPServer, DXPSession

PSK = b"dxp-dev-psk-change-me-before-prod"
HOST = "192.168.137.1"
PORT = 9000

server = DXPServer(host=HOST, port=PORT, psk=PSK)


@server.on_session
async def handle(session: DXPSession):
    print(f"[SESSION] device={session.device_id} session={session.session_id}")

    while True:
        data = await session.recv()
        print(f"[RX] {data!r}")
        await session.send(b"ACK")


asyncio.run(server.serve())
