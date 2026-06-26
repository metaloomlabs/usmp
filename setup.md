# Setting Up Your USMP Development Environment

Welcome to the USMP developer community! Whether you are looking to build a secure IoT product, experiment with microcontrollers, or test gateway APIs, this guide will walk you through setting up your environment for compiling, running, and testing USMP.

## 1. Network & Firewall Configuration

Since USMP runs over network connections (typically TCP on port `9000`), your host computer needs to allow incoming connections from your microcontrollers. If you don't configure this, the TCP handshake will fail immediately.

### Windows Defender Firewall

If you are developing on a Windows host machine, you will need to open port `9000` to allow the gateway server to receive data. 

To open port `9000` for both Public and Private networks, run this command in **PowerShell (with Administrator privileges)**:
```powershell
New-NetFirewallRule -DisplayName "USMP Gateway Server" -Direction Inbound -LocalPort 9000 -Protocol TCP -Action Allow -Profile Any
```

Or, if you prefer standard **cmd.exe (with Administrator privileges)**:
```cmd
netsh advfirewall firewall add rule name="USMP Gateway Server" dir=in action=allow protocol=TCP localport=9000 profile=any
```

> [!TIP]
> If you need to temporarily turn off the Public profile firewall for rapid testing on a local router network:
> ```powershell
> netsh advfirewall set publicprofile state off
> ```
> Remember to turn it back on once you're done:
> ```powershell
> netsh advfirewall set publicprofile state on
> ```

## 2. Python SDK Setup

The Python SDK is located under the `sdk/python/` directory. It uses `asyncio` to manage multiple secure device connections concurrently. We use `uv` as our package installer and virtual environment manager—it is fast, reliable, and PEP 517 compliant.

### 2.1 Get the Dependencies
Navigate to the Python SDK folder and sync your local environment:
```bash
cd sdk/python
uv sync
```
This automatically creates a virtual environment (`.venv`) and installs the dependencies required for USMP (such as `cryptography` and test libraries).

### 2.2 Verify with Unit Tests
Validate that your environment is correctly configured by running the test suite:
```bash
uv run pytest tests/ -v
```
You should see all 69 unit and integration tests pass successfully.

## 3. C Core & Port Setup

The USMP Core (`core/`) is written in pure, platform-independent C. It leverages `mbedtls` for handling public-key exchanges (Curve25519) and authenticated symmetric encryption (AES-256-GCM).

### ESP32 Component (ESP-IDF)
To include USMP as a native component in an ESP-IDF project:
1. Copy the `ports/usmp-esp32` directory into your project's `components/` folder.
2. In your `sdkconfig`, make sure `mbedtls` is enabled and configured. (Using ESP32's hardware-accelerated cryptographical engine is highly recommended for faster handshake processing).

### Arduino Port (ESP32 cores)
1. Zip up the files in `ports/usmp-arduino` to build a `.zip` library (or run the script `.\scripts\build-arduino-zip.ps1`).
2. Import the zip library into your Arduino IDE via **Sketch ➔ Include Library ➔ Add .ZIP Library**.
3. ESP32 Arduino cores natively bundle the `mbedtls` library, so the header files will resolve automatically without further setup.
