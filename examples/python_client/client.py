# examples/python_client/client.py
import asyncio
import logging
from usmp import USMPClient, ConnectionClosedError

# Configure logging
logging.basicConfig(
    level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s"
)

PSK = b"usmp-dev-psk-change-me-before-prod"
HOST = "127.0.0.1"
PORT = 9000


async def main():
    # Use a specific device ID (6 bytes)
    device_id = b"\x01\x02\x03\x04\x05\x06"

    logging.info(f"Initializing USMP Client for device {device_id.hex(':')}")
    client = USMPClient(host=HOST, port=PORT, psk=PSK, device_id=device_id)

    try:
        logging.info(f"Connecting to USMP server at {HOST}:{PORT}...")
        await client.connect()
        logging.info(f"Handshake successful! Session ID: {client.session_id}")

        # Send a few test messages
        for i in range(1, 6):
            message = f"Hello message {i}"
            logging.info(f"TX: {message}")
            await client.send(message.encode())

            # Wait for response
            response = await client.recv()
            logging.info(f"RX: {response.decode()}")

            # Sleep between messages
            await asyncio.sleep(2)

        # Send a manual PING to demonstrate keepalive
        logging.info("Sending PING...")
        await client.ping()
        logging.info("PING successful (received PONG)")

        # Disconnect cleanly (sends BYE frame)
        logging.info("Disconnecting cleanly...")
        await client.disconnect()
        logging.info("Disconnected.")

    except ConnectionRefusedError:
        logging.error("Failed to connect: Server is offline or port is closed.")
    except ConnectionClosedError:
        logging.error("Connection closed by the server prematurely.")
    except Exception as e:
        logging.error(f"An error occurred: {e}")


if __name__ == "__main__":
    asyncio.run(main())
