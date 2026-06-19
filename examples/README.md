# USMP Examples

Welcome to the Unified Secure Multi-transport Protocol (USMP) examples directory. This folder contains fully functional, plug-and-play codebases showing how to implement E2E encrypted sessions between IoT devices and servers.

## Examples Structure

```text
examples/
├── project_1/           ← Unified Multi-Platform Demo (Python Server + Arduino & ESP-IDF Clients)
├── secure_telemetry/    ← Dynamic JSON Sensor Telemetry Simulation (Python Client/Server)
├── python_client/       ← Minimal Python Asyncio Client implementation
└── python_server/       ← Minimal Python Asyncio Server implementation
```

---

## 1. Unified Multi-Platform Demo (`project_1`)

This is the recommended starting point for testing different target hardware. It contains:
* A python gateway/echo server.
* An ESP-IDF (ESP32) application that registers wifi and communicates.
* An Arduino (ESP32) sketch using the Arduino wrapper client.

👉 **Get Started:** Follow the detailed setup instructions in [project_1/README.md](project_1/README.md).

---

## 2. Secure Telemetry Demo (`secure_telemetry`)

This example demonstrates sending structured sensor readings (temperature and humidity) serialized as JSON payloads inside the encrypted GCM channel. 
* **Dynamic Interval Adjustment:** The server responds with command/control parameters, instructing the client to adjust its telemetry rate.
* **Premium Dashboard Format:** The server prints incoming readings in a colored terminal status box.

### Run the Server
In one terminal, run:
```bash
cd sdk/python
uv run python ../../examples/secure_telemetry/server.py
```

### Run the Client
In a second terminal, run:
```bash
cd sdk/python
uv run python ../../examples/secure_telemetry/client.py
```

---

## 3. Minimal Python Client & Server (`python_client` & `python_server`)

These directories contain standalone, minimal client and server implementations. They are perfect references for embedding USMP into your custom Python backends or scripts.

### Run the Server
```bash
cd sdk/python
uv run python ../../examples/python_server/server.py
```

### Run the Client
```bash
cd sdk/python
uv run python ../../examples/python_client/client.py
```

---

## Notes on Key Setup and Security

All development examples are preconfigured with the development PSK:
`usmp-dev-psk-change-me-before-prod`

> [!WARNING]
> This key is public and must **never** be used in production firmware or servers. 
> To generate a secure key for production deployments:
> ```bash
> python -c "import secrets; print(secrets.token_hex(32))"
> ```
