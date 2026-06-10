# examples/secure_telemetry/server.py
import asyncio
import json
import logging
from usmp import USMPServer, USMPSession, ConnectionClosedError

# Configure colorful console logging format
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s \033[1;36m[TELEMETRY_SERVER]\033[0m %(message)s",
)

PSK = b"usmp-dev-psk-change-me-before-prod"
HOST = "0.0.0.0"
PORT = 9000

server = USMPServer(host=HOST, port=PORT, psk=PSK)


@server.on_session
async def handle_device_session(session: USMPSession):
    device_name = f"device-{session.device_id[:4]}"
    logging.info(
        f"\033[1;32m[CONNECTED]\033[0m New secure session: "
        f"id={session.session_id} device={session.device_id} ({device_name})"
    )

    try:
        while True:
            # Wait for data from the device
            payload = await session.recv()
            try:
                data = json.loads(payload.decode("utf-8"))

                # Check message type
                msg_type = data.get("type", "unknown")
                if msg_type == "telemetry":
                    metrics = data.get("metrics", {})
                    temp = metrics.get("temperature", 0.0)
                    humidity = metrics.get("humidity", 0.0)
                    status = data.get("status", "OK")

                    # Print telemetry in a premium dashboard format
                    print(
                        f"\033[1;34m┌─── {device_name.upper()} Telemetry ───────────────────┐\033[0m\n"
                        f"\033[1;34m│\033[0m Temperature : {temp:5.2f} °C                        \033[1;34m│\033[0m\n"
                        f"\033[1;34m│\033[0m Humidity    : {humidity:5.2f} %                         \033[1;34m│\033[0m\n"
                        f"\033[1;34m│\033[0m System State: {status:<15}                \033[1;34m│\033[0m\n"
                        f"\033[1;34m└──────────────────────────────────────────────┘\033[0m"
                    )

                    # Respond with status acknowledgement and command if needed
                    response = {
                        "status": "received",
                        "command": "interval_sync",
                        "interval_sec": 5,
                    }
                    await session.send(json.dumps(response).encode("utf-8"))

                else:
                    logging.warning(f"[{device_name}] Unknown message type: {msg_type}")
                    await session.send(
                        b'{"status": "error", "message": "unknown_type"}'
                    )

            except json.JSONDecodeError:
                logging.error(
                    f"[{device_name}] Received invalid JSON payload: {payload}"
                )
                await session.send(b'{"status": "error", "message": "invalid_json"}')

    except ConnectionClosedError:
        logging.info(
            f"\033[1;31m[DISCONNECTED]\033[0m Session closed for device: {session.device_id}"
        )
    except Exception as e:
        logging.error(f"Error in session for device {session.device_id}: {e}")


async def main():
    logging.info(f"Starting Secure Telemetry Server on {HOST}:{PORT}...")
    await server.serve()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logging.info("Server stopped by user.")
