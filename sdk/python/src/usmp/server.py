"""USMP Development & Echo CLI Server.

Provides an instant command-line server for testing Arduino, ESP32, and other USMP devices:
    python -m usmp.server --port 9000 --psk "my-secret-key-16b" --echo
"""

from __future__ import annotations

import argparse
import asyncio
import logging
import os
import sys
from collections.abc import Sequence

from ._logging import setup_logging
from ._server import USMPServer
from ._session import USMPSession
from .errors import ConnectionClosedError, USMPError
from .types import USMPProtocol

logger = logging.getLogger("usmp.cli")


def parse_psk(psk_arg: str) -> bytes:
    """Parse PSK from command-line argument (plain string or hex:<bytes>)."""
    if not psk_arg:
        return b""
    if psk_arg.startswith("hex:"):
        try:
            return bytes.fromhex(psk_arg[4:])
        except ValueError as err:
            raise ValueError(f"Invalid hex PSK string: {err}") from err
    if len(psk_arg) == 64:
        try:
            return bytes.fromhex(psk_arg)
        except ValueError:
            pass
    return psk_arg.encode("utf-8")


def print_banner(host: str, port: int, proto: str, echo: bool, psk_len: int) -> None:
    """Print a clean startup banner."""
    mode_str = "Echo Server (replies to all data)" if echo else "Sink Server (logs received data)"
    border = "═" * 58
    sys.stdout.write(f"\n╔{border}╗\n")
    sys.stdout.write(f"║ {'USMP Development Server':^56} ║\n")
    sys.stdout.write(f"╠{border}╣\n")
    sys.stdout.write(f"║  • Protocol:   {proto.upper():<43} ║\n")
    sys.stdout.write(f"║  • Address:    {f'{host}:{port}':<43} ║\n")
    sys.stdout.write(f"║  • Mode:       {mode_str:<43} ║\n")
    sys.stdout.write(f"║  • PSK:        {f'configured ({psk_len} bytes)':<43} ║\n")
    sys.stdout.write(f"╚{border}╝\n")
    sys.stdout.write("Listening for incoming device connections... (Ctrl+C to stop)\n\n")
    sys.stdout.flush()


def build_parser() -> argparse.ArgumentParser:
    """Construct command-line argument parser."""
    parser = argparse.ArgumentParser(
        prog="usmp.server",
        description="USMP Development & Echo Server for IoT devices.",
    )
    parser.add_argument(
        "--host",
        default="0.0.0.0",  # noqa: S104
        help="Host interface to bind to (default: 0.0.0.0)",
    )
    parser.add_argument(
        "--port",
        "-p",
        type=int,
        default=9000,
        help="Port to listen on (default: 9000)",
    )
    parser.add_argument(
        "--psk",
        "-k",
        type=str,
        default="",
        help="Pre-shared key (raw string or 'hex:<hexbytes>'). Minimum 16 bytes.",
    )
    parser.add_argument(
        "--protocol",
        choices=["tcp", "udp"],
        default="tcp",
        help="Transport protocol (default: tcp)",
    )
    parser.add_argument(
        "--echo",
        "-e",
        action="store_true",
        help="Echo received application data back to client",
    )
    parser.add_argument(
        "--log-level",
        "-l",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
        default="INFO",
        help="Log verbosity level (default: INFO)",
    )
    parser.add_argument(
        "--generate-psk",
        action="store_true",
        help="Generate a cryptographically random 32-byte PSK and exit",
    )
    return parser


async def run_server(args: argparse.Namespace, psk_bytes: bytes) -> None:
    """Initialize and run the USMPServer until interrupted."""
    proto = USMPProtocol.TCP if args.protocol.lower() == "tcp" else USMPProtocol.UDP
    server = USMPServer(
        host=args.host,
        port=args.port,
        psk=psk_bytes,
        protocol=proto,
    )

    @server.on_session
    async def handle_session(session: USMPSession) -> None:
        dev = session.device_id
        logger.info("🤝 Device connected: %s (session %s)", dev, session.session_id)
        try:
            while session.is_connected:
                try:
                    data = await session.recv()
                except ConnectionClosedError:
                    logger.info("👋 Device %s sent BYE (graceful close)", dev)
                    break
                except (USMPError, OSError) as e:
                    logger.warning("Session recv error from %s: %s", dev, e)
                    break

                try:
                    text = data.decode("utf-8")
                    disp = f"'{text}'"
                except UnicodeDecodeError:
                    disp = f"hex:{data.hex()}"
                logger.info("📩 [%s] Received (%d bytes): %s", dev, len(data), disp)

                if args.echo:
                    await session.send(data)
                    logger.info("📤 [%s] Echoed (%d bytes)", dev, len(data))
        finally:
            logger.info("🔌 Device disconnected: %s", dev)

    print_banner(args.host, args.port, args.protocol, args.echo, len(psk_bytes))
    await server.serve()


def main(argv: Sequence[str] | None = None) -> int:
    """CLI entrypoint."""
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.generate_psk:
        key = os.urandom(32)
        print("Generated random 32-byte (256-bit) Pre-Shared Key (PSK):")
        print(f"  Hex:    {key.hex()}")
        print(f"  Python: {key!r}")
        print("\nUsage example:")
        print(f"  python -m usmp.server --psk hex:{key.hex()} --echo")
        return 0

    try:
        psk_bytes = parse_psk(args.psk)
    except ValueError as err:
        sys.stderr.write(f"Error: {err}\n")
        return 1

    if not psk_bytes:
        sys.stderr.write("Error: --psk <key> is required. Use --generate-psk to create one.\n")
        return 1

    if len(psk_bytes) < 16:
        sys.stderr.write(
            f"Error: PSK must be at least 16 bytes long (got {len(psk_bytes)} bytes).\n"
        )
        return 1

    setup_logging(level=args.log_level)

    try:
        asyncio.run(run_server(args, psk_bytes))
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
