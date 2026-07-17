# examples/project_tcp/server/server.py
import asyncio
import logging
from usmp import USMPServer, USMPSession, USMPProtocol, ConnectionClosedError

# The SDK logs via the "usmp" logger and ships a NullHandler, so it stays silent
# until the application configures logging. Route its INFO/WARNING/ERROR to the
# console so server-side events (handshake failures, timeouts, errors) are visible.
logging.basicConfig(level=logging.INFO)

# WARNING: Do NOT use hardcoded PSK constants in production environments.
# In production, provision and load the PSK from a secure storage mechanism
# (e.g. environment variables, secure database, or key vaults).
PSK = b"usmp-dev-psk-change-me-before-prod"
HOST = "0.0.0.0"
PORT = 9000

server = USMPServer(host=HOST, port=PORT, psk=PSK, protocol=USMPProtocol.TCP)


@server.on_session
async def handle(session: USMPSession):
    peername = session._writer.get_extra_info("peername")
    peer_ip = peername[0] if peername else "unknown"
    print(f"[SESSION] device={session.device_id} session={session.session_id} ip={peer_ip}")
    try:
        while True:
            data = await session.recv()
            text = data.decode().strip()
            print(f"[RX] {text} -> sending back hello from server")
            await session.send(b"hello from server")
    except ConnectionClosedError:
        print(f"[CLOSED] {session.device_id}")


asyncio.run(server.serve())
