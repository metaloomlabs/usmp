# USMP Environment Setup Guide

This guide describes how to set up your environment for developing, testing, and compiling USMP.

---

## 1. Network & Firewall Configuration

If running the USMP server locally, ensure the target port (default `9000`) is accessible through your firewall.

### Windows Defender Firewall

To allow inbound TCP traffic on port 9000:
```powershell
netsh advfirewall firewall add rule name="USMP Server" dir=in action=allow protocol=TCP localport=9000 profile=any
```

To disable/enable the public profile firewall (for development only):
```powershell
# Disable
netsh advfirewall set publicprofile state off

# Enable
netsh advfirewall set publicprofile state on
```

---

## 2. Python SDK Setup

The Python SDK is managed using `uv`. Make sure you have `uv` installed.

### 2.1 Install Dependencies
Run from the `sdk/python` directory to install dependencies and establish the virtual environment:
```bash
cd sdk/python
uv sync
```

### 2.2 Running Tests
Validate the installation by running the unit test suite:
```bash
uv run pytest
```

---

## 3. C Core and Port Setup

The C Core implementation uses `mbedtls` for cryptographic routines.

### ESP32 Component
To use USMP as an ESP-IDF component, copy the `usmp-esp32-component` directory to your project's `components/` directory, and configure the required `mbedtls` configurations in your sdkconfig (e.g. enabling hardware acceleration if available).

### Arduino Port
1. Compile the files under `ports/usmp-arduino/src` into a zip file or copy them to your Arduino libraries directory.
2. Ensure you have the `mbedtls` library installed or configured for your target board (ESP32-based Arduino boards include mbedtls natively).
