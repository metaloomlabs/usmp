# Project 1: Multi-Platform USMP Echo Demo

This project provides a complete plug-and-play echo application running across three environments:
1. **Python Gateway Server:** Receives encrypted data, prints it, and replies.
2. **ESP32 (ESP-IDF) Client:** Connects over TCP, runs the handshake, and sends telemetry.
3. **Arduino (ESP32) Client:** An alternative client sketch for Arduino IDE developers.

---

## Architecture Diagram

```text
  ┌─────────────────┐
  │  Python Server  │◀───┐
  │  (Port 9000)    │    │ (USMP Session over TCP)
  └─────────────────┘    │
           ▲             │
           │             ▼
  ┌─────────────────┐  ┌─────────────────┐
  │  ESP32 Client   │  │ Arduino Client  │
  │  (ESP-IDF App)  │  │  (USMP Sketch)  │
  └─────────────────┘  └─────────────────┘
```

---

## Step 1: Start the Python Gateway Server

We'll start the server first so that our microcontrollers can connect to it immediately when they boot.

1. Ensure your host machine and microcontrollers are on the same Wi-Fi network.
2. Find the local IP address of your host machine (e.g. `192.168.1.100`).
3. Run the gateway server:
   ```bash
   cd sdk/python
   uv run python ../../examples/project_1/server/server.py
   ```
   You should see:
   ```text
   [USMP] Listening on 0.0.0.0:9000
   ```

---

## Step 2: Configure & Deploy the Microcontroller Client

Choose **either** the ESP-IDF variant or the Arduino IDE variant below.

### Option A: Arduino Client (ESP32)

1. Open your Arduino IDE.
2. Zip the [ports/usmp-arduino/](file:///c:/Users/main/codinways/MetaLoom/products/usmp/ports/usmp-arduino) directory to build `usmp-arduino.zip`, or copy the directory directly to your Arduino `libraries/USMP` folder.
3. Open the sketch file [arduino.ino](file:///c:/Users/main/codinways/MetaLoom/products/usmp/examples/project_1/arduino/arduino.ino) in your IDE.
4. Modify the config block at the top of the file:
   ```cpp
   #define SERVER_IP   "your-server-ip-here"   // e.g. "192.168.1.100"
   #define WIFI_SSID   "YourWifiName"
   #define WIFI_PASS   "YourWifiPassword"
   ```
5. Select your ESP32 board and upload the code.
6. Open the **Serial Monitor** (set baud rate to `115200`). You should see:
   ```text
   [USMP] Connecting to WiFi: YourWifiName
   [USMP] WiFi connected — IP: 192.168.1.120
   [USMP] TCP Connected
   Connected!
   Device:  ab:cd:ef:01:02:03
   Session: 5f3b7c2a8e9d0a1b2c3d4e5f6a7b8c9d
   ```

---

### Option B: ESP32 Client (ESP-IDF)

1. Navigate to the ESP32 project directory:
   ```bash
   cd examples/project_1/esp32
   ```
2. Open the application code [app.c](file:///c:/Users/main/codinways/MetaLoom/products/usmp/examples/project_1/esp32/main/app.c).
3. Update the `server_ip` variable to match your host machine IP:
   ```c
   const char *server_ip = "your-server-ip-here"; // e.g. "192.168.1.100"
   ```
4. Configure your Wi-Fi credentials in Kconfig:
   ```bash
   idf.py menuconfig
   ```
   * Navigate to `Example Connection Configuration` and enter your Wi-Fi SSID and Password.
5. Build, flash, and monitor:
   ```bash
   idf.py build flash monitor
   ```
   Upon successful boot and connection, you should see the handshake details output to the serial console:
   ```text
   I (1230) APP: Initializing Wi-Fi
   I (2450) APP: TCP connected (attempt 1)
   I (2460) USMP_HS: HELLO sent
   I (2480) USMP_HS: CHALLENGE received
   I (2520) USMP_HS: X25519 shared secret computed
   I (2525) USMP_HS: Session key derived
   I (2530) USMP_HS: HELLO_ACK sent
   I (2550) USMP_HS: Server authenticated OK
   I (2555) USMP_HS: SESSION_OK
   I (2560) USMP: Session established
   I (2565) USMP_SESSION: TX seq=0 len=16
   I (2570) APP: Message sent
   ```

---

## Step 3: Observe Server Outputs

Once either client connects, your Python server console will output:
```text
[SESSION] device=ab:cd:ef:01:02:03 session=5f3b7c2a8e9d0a1b2c3d4e5f6a7b8c9d ip=192.168.1.120
[RX] hello from arduino -> sending back hello from server
```
The server will automatically decrypt all messages sent by the client, and encrypt its response back to the client.
