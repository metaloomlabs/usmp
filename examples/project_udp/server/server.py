# examples/project_udp/server/server.py
import asyncio
from usmp import USMPServer, USMPSession, USMPProtocol, ConnectionClosedError

# WARNING: Do NOT use hardcoded PSK constants in production environments.
# In production, provision and load the PSK from a secure storage mechanism.
PSK = b"usmp-dev-psk-change-me-before-prod"
HOST = "0.0.0.0"
PORT = 9000

server = USMPServer(host=HOST, port=PORT, psk=PSK, protocol=USMPProtocol.UDP)


@server.on_session
async def handle(session: USMPSession):
    peername = session._writer.get_extra_info("peername")
    peer_ip = peername[0] if peername else "unknown"
    print(f"[SESSION-UDP] device={session.device_id} session={session.session_id} ip={peer_ip}")
    try:
        while True:
            data = await session.recv()
            text = data.decode().strip()
            print(f"[RX-UDP] {text} -> sending back hello from server")
            await session.send(b"hello from server via UDP")
    except ConnectionClosedError:
        print(f"[CLOSED-UDP] {session.device_id}")


print("USMP UDP Server starting...")
asyncio.run(server.serve())
