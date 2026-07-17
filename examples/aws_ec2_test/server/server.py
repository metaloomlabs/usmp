# examples/aws_ec2_test/server/server.py
import asyncio
import argparse
import logging
import os
import socket
from usmp import USMPServer, USMPSession, USMPProtocol, ConnectionClosedError

# The SDK logs via the "usmp" logger and ships a NullHandler, so it stays silent
# until the application configures logging. Route its INFO/WARNING/ERROR to the
# console so server-side events (handshake failures, timeouts, errors) are visible.
logging.basicConfig(level=logging.INFO)

# Default configuration settings
DEFAULT_PSK = b"usmp-dev-psk-change-me-before-prod"
DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 9000

def get_local_ips():
    """Helper to detect local IP addresses for user convenience."""
    ips = []
    try:
        # Get host name
        hostname = socket.gethostname()
        # Resolve all IP addresses associated with host name
        for ip in socket.getaddrinfo(hostname, None):
            addr = ip[4][0]
            if ":" not in addr and addr != "127.0.0.1":  # Filter IPv4 and non-localhost
                ips.append(addr)
    except Exception:
        pass
    return list(set(ips))

async def handle_client(session: USMPSession):
    """
    Callback handler executed for every new USMP session established.
    """
    peername = session._writer.get_extra_info("peername")
    peer_ip = peername[0] if peername else "unknown"
    print(f"\n[SESSION ESTABLISHED]")
    print(f"  Device ID:  {session.device_id}")
    print(f"  Session ID: {session.session_id}")
    print(f"  Client IP:  {peer_ip}")
    
    try:
        while True:
            # Wait to receive decrypted data from the device
            data = await session.recv()
            text = data.decode('utf-8', errors='ignore').strip()
            print(f"[RX from {session.device_id}]: {text}")
            
            # Send an encrypted reply back to the device
            response = f"Echo from EC2: {text}"
            print(f"[TX to {session.device_id}]: {response}")
            await session.send(response.encode('utf-8'))
            
    except ConnectionClosedError:
        print(f"[SESSION CLOSED] Device disconnected: {session.device_id}")
    except Exception as e:
        print(f"[SESSION ERROR] Error handling {session.device_id}: {e}")

async def main():
    # Parse command line arguments
    parser = argparse.ArgumentParser(description="USMP EC2 Gateway Server")
    parser.add_argument("--host", default=DEFAULT_HOST, help="Host interface to bind to")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="Port to listen on")
    parser.add_argument("--psk", default=os.getenv("USMP_PSK"), help="Pre-shared key (string)")
    args = parser.parse_args()

    # Load PSK (prioritize argument, fallback to env, fallback to default)
    if args.psk:
        psk_bytes = args.psk.encode('utf-8')
        print("[CONFIG] Loaded PSK from command line arguments.")
    elif os.getenv("USMP_PSK"):
        psk_bytes = os.getenv("USMP_PSK").encode('utf-8')
        print("[CONFIG] Loaded PSK from environment variable (USMP_PSK).")
    else:
        psk_bytes = DEFAULT_PSK
        print("[WARNING] Using DEFAULT development PSK. DO NOT use this in production!")

    # Standardize PSK length (must be at least 16 bytes per security constraints)
    if len(psk_bytes) < 16:
        print(f"[CRITICAL ERROR] PSK must be at least 16 bytes! (Current length: {len(psk_bytes)})")
        return

    # Initialize the USMP Server (TCP transport is standard)
    server = USMPServer(
        host=args.host,
        port=args.port,
        psk=psk_bytes,
        protocol=USMPProtocol.TCP
    )
    
    # Register the session handler callback
    server.on_session(handle_client)

    print("=" * 60)
    print("                USMP SECURE SERVER (EC2)")
    print("=" * 60)
    print(f"Listening on: {args.host}:{args.port}")
    print(f"Protocol:     TCP")
    
    # Show detected local network IPs to help user check configurations
    local_ips = get_local_ips()
    if local_ips:
        print("Detected local IP(s) on this instance:")
        for ip in local_ips:
            print(f"  - {ip}")
    print("Note: In AWS EC2, you must configure Security Groups to allow inbound TCP")
    print(f"traffic on port {args.port} from your microcontroller's external IP.")
    print("=" * 60)
    print("Starting server... Press Ctrl+C to stop.")

    try:
        await server.serve()
    except asyncio.CancelledError:
        print("\nStopping server...")
    except Exception as e:
        print(f"\nServer error: {e}")

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nServer shutdown gracefully.")
