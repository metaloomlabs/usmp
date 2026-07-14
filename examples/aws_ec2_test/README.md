# Testing USMP on AWS EC2

This guide walks you through setting up a public test server on an **AWS EC2** instance and connecting a local **ESP32** microcontroller (using either the **Arduino IDE** or the native **ESP-IDF** framework) to establish a secure, end-to-end encrypted USMP session over the Internet.

```
┌──────────────────────┐             Public Internet             ┌──────────────────┐
│ ESP32 Client         │ ──────────────────────────────────────> │  AWS EC2 Server  │
│ (Arduino or ESP-IDF) │    TCP Port 9000 (AES-GCM-256 Secure)   │ (Python Gateway) │
└──────────────────────┘                                         └──────────────────┘
```

---

## Directory Structure

```text
examples/aws_ec2_test/
├── README.md               # This setup and deployment guide
├── server/
│   └── server.py           # Python server running on EC2
├── arduino/
│   └── arduino.ino         # Arduino IDE client sketch for ESP32
└── esp32/                  # ESP-IDF project files for ESP32
    ├── CMakeLists.txt
    ├── sdkconfig.defaults
    └── main/
        ├── CMakeLists.txt
        ├── app.c           # Main ESP-IDF client logic
        ├── wifi.c          # Wi-Fi helper implementation
        └── wifi.h
```

---

## Prerequisites

Before starting, make sure you have:
1. An active **AWS Account**.
2. An **ESP32 development board** connected to your local machine.
3. Depending on your preferred framework:
   - **Arduino IDE**: Install the IDE and locate the prepackaged Arduino library zip `usmp-1.0.1-arduino.zip` in the root of this project.
   - **ESP-IDF**: Install the ESP-IDF toolchain (v5.0+) and make sure `idf.py` is available in your terminal path.

---

## Step 1: Launch an AWS EC2 Instance

1. Log in to the [AWS Management Console](https://aws.amazon.com/console/).
2. Navigate to the **EC2 Dashboard** and click **Launch instance**.
3. Configure the following settings:
   - **Name**: `usmp-test-server`
   - **Application and OS Image (AMI)**: Select **Ubuntu** (e.g., *Ubuntu Server 24.04 LTS*, Free tier eligible).
   - **Architecture**: `x86_64` (default).
   - **Instance type**: `t2.micro` or `t3.micro` (Free tier eligible).
   - **Key pair (login)**: Choose an existing key pair or click **Create new key pair** (download and save the `.pem` file safely).
4. Leave other settings as default and click **Launch instance**.

---

## Step 2: Configure the Security Group (Open Port 9000)

By default, AWS blocks all incoming traffic to your EC2 instance except SSH (Port 22). To allow the ESP32 to communicate with the USMP server, you must open TCP port `9000`.

1. In the EC2 Dashboard, click on your running instance `usmp-test-server`.
2. Select the **Security** tab at the bottom and click on the link under **Security groups** (e.g., `sg-xxxxxxxxxxxxxxxxx`).
3. Click **Edit inbound rules**.
4. Click **Add rule** and set up the following two rules:
   - **Rule 1 (For SSH)**:
     - **Type**: `SSH`
     - **Port Range**: `22`
     - **Source**: `My IP` (recommended for safety) or `Anywhere-IPv4` (`0.0.0.0/0`).
   - **Rule 2 (For USMP Server)**:
     - **Type**: `Custom TCP`
     - **Port Range**: `9000`
     - **Source**: `Anywhere-IPv4` (`0.0.0.0/0`).
5. Click **Save rules**.

---

## Step 3: Connect and Prepare the EC2 Instance

1. Open a terminal (PowerShell, Command Prompt, or Bash) on your local machine and navigate to the directory where your downloaded `.pem` private key is saved.
2. Update permissions for your key (on Linux/macOS only):
   ```bash
   chmod 400 your-key.pem
   ```
3. Connect to the EC2 instance via SSH (replace `your-key.pem` and the IP address with your actual details):
   ```bash
   ssh -i your-key.pem ubuntu@your-ec2-public-ip
   ```
4. Once logged in, update the package manager and install Python 3 and its virtual environment environment package:
   ```bash
   sudo apt update && sudo apt upgrade -y
   sudo apt install python3 python3-pip python3-venv git -y
   ```

---

## Step 4: Run the USMP Server on EC2

To run the server, we will install `usmp` from PyPI and run the server script directly on your EC2 instance.

### Option A: Using standard pip
1. Create a directory and virtual environment:
   ```bash
   mkdir usmp-server && cd usmp-server
   python3 -m venv .venv
   source .venv/bin/activate
   ```
2. Install the `usmp` package from PyPI:
   ```bash
   pip install usmp
   ```

### Option B: Using uv (Recommended for speed)
1. Initialize a new project and add the `usmp` dependency:
   ```bash
   mkdir usmp-server && cd usmp-server
   uv init
   uv add usmp
   ```

### Running the server script
1. Create the `server.py` file on your EC2 instance (e.g., run `nano server.py` and paste the contents of [server.py](file:///c:/Users/main/codinways/MetaLoom/products/usmp/examples/aws_ec2_test/server/server.py) into it).
2. Start the server:
   * **If using pip/venv**:
     ```bash
     python3 server.py
     ```
   * **If using uv**:
     ```bash
     uv run server.py
     ```
   *You should see a message indicating the server is listening on port 9000 and displaying the local IP address.*

---

## Step 5: Configure and Upload Client Firmware

Choose **one** of the client options below to flash onto your ESP32.

### Option A: Using Arduino IDE

1. Run the local build script to ensure you have the latest offline ZIP library:
   - On Windows: Run `.\scripts\build-arduino-zip.ps1` in PowerShell.
   - Alternatively, locate the pre-built `usmp-1.0.1-arduino.zip` in the root folder.
2. In the **Arduino IDE**, import the ZIP library:
   - Go to **Sketch** ➔ **Include Library** ➔ **Add .ZIP Library...**
   - Select the `usmp-1.0.1-arduino.zip` file.
3. Open the file [arduino.ino](file:///c:/Users/main/codinways/MetaLoom/products/usmp/examples/aws_ec2_test/arduino/arduino.ino) in your Arduino IDE.
4. Modify the config parameters in the sketch:
   - **`EC2_PUBLIC_IP`**: Put the **Public IPv4 address** of your running EC2 instance.
   - **`WIFI_SSID`**: Enter your local Wi-Fi name.
   - **`WIFI_PASS`**: Enter your local Wi-Fi password.
   - **`PSK`**: Match this with your server key (default: `usmp-dev-psk-change-me-before-prod`).
5. Select your ESP32 board and the matching COM port, then click **Upload**.

### Option B: Using ESP-IDF (Native C)

1. Open a command line on your local machine and navigate to the ESP-IDF example directory:
   ```bash
   cd examples/aws_ec2_test/esp32
   ```
2. Open the file [wifi.c](file:///c:/Users/main/codinways/MetaLoom/products/usmp/examples/aws_ec2_test/esp32/main/wifi.c) and configure your Wi-Fi credentials:
   ```c
   #define WIFI_SSID "YOUR_WIFI_SSID"
   #define WIFI_PASS "YOUR_WIFI_PASSWORD"
   ```
3. Open the file [app.c](file:///c:/Users/main/codinways/MetaLoom/products/usmp/examples/aws_ec2_test/esp32/main/app.c) and configure your EC2 details and Pre-Shared Key (PSK):
   ```c
   #define EC2_PUBLIC_IP "YOUR_EC2_PUBLIC_IP"
   #define PSK "usmp-dev-psk-change-me-before-prod"
   ```
4. Set your ESP-IDF build target (e.g. for standard ESP32):
   ```bash
   idf.py set-target esp32
   ```
5. Build, flash, and open the serial monitor (replace `COMx` or `/dev/ttyUSBx` with your ESP32's serial port):
   ```bash
   idf.py -p COMx flash monitor
   ```

---

## Step 6: Verify Connection and Logs

### On the ESP32 Serial Monitor
You should see output similar to this as the handshake completes:
```text
Connecting to Wi-Fi: YourNetwork
Connecting to USMP EC2 Server: 13.62.222.96:9000
[INFO] Connecting to 13.62.222.96 on port 9000...
[INFO] Socket connected. Sending handshake initiator...
[INFO] Handshake response received. Validating peer...
[SUCCESS] Secure USMP session established!
  Device ID:  device_xxxxxxxxxxxx
  Session ID: session_xxxxxxxxxxxx
Sending: ESP32 Ping #1 (Uptime: 5s)
  Sent successfully (encrypted).
Received from EC2: Echo from EC2: ESP32 Ping #1 (Uptime: 5s)
```

### On the EC2 Instance (Python Terminal)
You will see session logs appearing in real-time as the connection is made:
```text
============================================================
                USMP SECURE SERVER (EC2)
============================================================
Listening on: 0.0.0.0:9000
Protocol:     TCP
============================================================
Starting server... Press Ctrl+C to stop.

[SESSION ESTABLISHED]
  Device ID:  device_xxxxxxxxxxxx
  Session ID: session_xxxxxxxxxxxx
  Client IP:  73.14.XX.XX

[RX from device_xxxxxxxxxxxx]: Hello EC2, this is ESP32 via USMP!
[TX to device_xxxxxxxxxxxx]: Echo from EC2: Hello EC2, this is ESP32 via USMP!
[RX from device_xxxxxxxxxxxx]: ESP32 Ping #1 (Uptime: 5s)
[TX to device_xxxxxxxxxxxx]: Echo from EC2: ESP32 Ping #1 (Uptime: 5s)
```

---

## Troubleshooting

- **Handshake fails or times out**:
  - Double check your EC2 Security Group inbound rules. Make sure the port is `9000` and the protocol is `TCP`.
  - Check if the EC2 instance is actually running and you copied the **Public IP** (not the private IP).
  - Ensure the ESP32 is successfully connecting to Wi-Fi first.
- **Handshake verification fails**:
  - Ensure the `PSK` defined in `arduino.ino` or `app.c` matches the PSK used on the EC2 server (`server.py`).
- **ESP-IDF Build Issues**:
  - Check that the `usmp-esp32` port directory exists under `ports/usmp-esp32` relative to this repository.
