# examples/secure_telemetry/client.py
import asyncio
import json
import logging
import random
from usmp import USMPClient, ConnectionClosedError

# Configure colorful console logging format
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s \033[1;35m[TELEMETRY_CLIENT]\033[0m %(message)s"
)

PSK = b"usmp-dev-psk-change-me-before-prod"
HOST = "127.0.0.1"
PORT = 9000

async def run_sensor_loop(client: USMPClient):
    # Starting simulated values
    temperature = 22.5
    humidity = 45.0
    interval = 3.0  # seconds between telemetry reports

    logging.info("Starting sensor telemetry simulation loop...")
    while True:
        # Simulate slight sensor drift
        temperature += random.uniform(-0.5, 0.5)
        humidity = max(0.0, min(100.0, humidity + random.uniform(-1.0, 1.0)))

        # Build JSON telemetry payload
        payload = {
            "type": "telemetry",
            "status": "HEALTHY",
            "metrics": {
                "temperature": temperature,
                "humidity": humidity
            }
        }

        logging.info(f"TX: Temp={temperature:.2f} °C, Humidity={humidity:.2f} %")
        await client.send(json.dumps(payload).encode("utf-8"))

        # Wait for server response/ACK
        try:
            response_data = await client.recv()
            response = json.loads(response_data.decode("utf-8"))
            logging.info(f"RX ACK: {response}")
            
            # Dynamically update interval from server command if provided
            if "interval_sec" in response:
                interval = float(response["interval_sec"])
        except ConnectionClosedError:
            logging.warning("Connection closed during receive. Exiting telemetry loop.")
            break
        except Exception as e:
            logging.error(f"Error receiving telemetry response: {e}")

        # Sleep before next telemetry reading
        await asyncio.sleep(interval)

async def main():
    device_id = bytes([random.randint(0, 255) for _ in range(6)])
    logging.info(f"Initializing client with random Device ID: {device_id.hex(':')}")

    while True:
        client = USMPClient(host=HOST, port=PORT, psk=PSK, device_id=device_id)
        try:
            logging.info(f"Attempting to connect to telemetry server at {HOST}:{PORT}...")
            await client.connect()
            logging.info(f"\033[1;32m[CONNECTED]\033[0m Session active: {client.session_id}")

            # Run the telemetry loop
            await run_sensor_loop(client)

        except ConnectionRefusedError:
            logging.warning("Server is offline. Retrying in 5 seconds...")
            await asyncio.sleep(5)
        except ConnectionClosedError:
            logging.warning("Disconnected from server. Reconnecting in 3 seconds...")
            await asyncio.sleep(3)
        except Exception as e:
            logging.error(f"Unexpected error: {e}. Reconnecting in 5 seconds...")
            await asyncio.sleep(5)
        finally:
            # Clean up client socket/session
            try:
                await client.disconnect()
            except Exception:
                pass

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logging.info("Client stopped by user.")
