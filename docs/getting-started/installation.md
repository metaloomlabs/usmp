# Installation & Environment Setup

This tutorial guides you through installing the USMP development libraries and generating the Pre-Shared Keys (PSK) required to secure your devices.

USMP has two primary components:
1.  **Pure C Core**: Compiled on the microcontrollers (available via the ESP Component Registry and a pre-packaged Arduino ZIP).
2.  **Python asyncio SDK**: Runs on your gateway or server (available via PyPI).

---

## 1. Install the Python asyncio SDK (Gateway / Server)

The Python SDK manages the server gateways, registers connected devices, and performs the heavy lifting for decryption and key management. It requires **Python 3.11 or later**.

=== "Using pip"
    ```bash
    pip install usmp
    ```

=== "Using uv (Recommended)"
    ```bash
    uv add usmp
    ```

To verify the installation succeeded:
```bash
python -c "import usmp; print(usmp.__version__)"
```

---

## 2. Install the ESP32 Component (ESP-IDF)

For native C development on Espressif chips using ESP-IDF (v5.0 or later), USMP is published directly in the Espressif Component Registry.

Open your ESP-IDF project terminal and run:
```bash
idf.py add-dependency "metaloomlabs/usmp"
```

This will automatically download and append USMP to your project's `main/idf_component.yml` dependency declaration. When you next run `idf.py build`, the compiler will automatically fetch and compile the component.

---

## 3. Install the Arduino Library (ESP32 Cores)

For Arduino C++ development, the USMP library is packaged as an offline ZIP archive.

1.  Locate the pre-built ZIP package: `usmp-0.5.1-arduino.zip` (found in your workspace artifacts/releases folder).
2.  Open the **Arduino IDE** (v2.0 or later).
3.  Go to **Sketch** ➔ **Include Library** ➔ **Add .ZIP Library...**
4.  Navigate to and select the `usmp-0.5.1-arduino.zip` file.
5.  Click **Open** to import the library.

To verify installation, you can inspect your libraries folder. The IDE will now recognize the `#include <USMP.h>` header.

---

## 4. Generate a Secure Pre-Shared Key (PSK)

USMP requires both the client device and the server to share a Pre-Shared Key (PSK) to establish the initial handshake trust. 

> [!WARNING]
> **Never use generic or weak keys (such as `123456` or `my-psk`) in production.** 
> Use cryptographically strong random bytes.

You can quickly generate a secure 32-byte (256-bit) hexadecimal key using Python's built-in `secrets` module:

```bash
python -c "import secrets; print(secrets.token_hex(32))"
```

This will output a 64-character hexadecimal string, for example:
`2a8d56b3e410ff02ad84620ea1901a88b50df3651c4a0910bc4f794d21e8e4bc`

Keep this key secure. You will pass it to both your gateway server and your microcontrollers in the next tutorials.

---

## Next Steps

Now that your tools are installed and you have a secure PSK, proceed to build your first connection:
*   **[Tutorial 1: Your First TCP Tunnel](tutorial-tcp.md)** — Connect your ESP32 or Arduino to a Python server using TCP sockets.
*   **[Tutorial 2: Going Connectionless (UDP)](tutorial-udp.md)** — Learn how to set up and run USMP over UDP.

