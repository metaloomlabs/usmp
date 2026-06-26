# Installing USMP

USMP is built in two primary parts: a **pure C core** (for embedded devices) and a **Python asyncio SDK** (for gateways, servers, and testing).

Below you will find the setup instructions for each platform.

## Option 1: ESP32 (ESP-IDF Component)

If you are developing native applications using the Espressif IoT Development Framework (ESP-IDF), USMP is designed to drop in as a standard component.

### What You'll Need

* **ESP-IDF** (v5.0 or later recommended).
* **CMake** (v3.16 or later).

### Setup Instructions

1. **Clone the USMP repository** into your project's components folder, or clone it elsewhere and refer to it:

   ```bash
   git clone https://github.com/metaloomlabs/usmp.git
   ```

2. **Configure your Project's `CMakeLists.txt`** to declare the component path:

   ```cmake
   cmake_minimum_required(VERSION 3.16)

   # Tell CMake where to find the USMP component
   set(EXTRA_COMPONENT_DIRS
       "/path/to/usmp/ports/usmp-esp32"
   )

   include($ENV{IDF_PATH}/tools/cmake/project.cmake)
   project(your_project)
   ```

3. **Declare Dependency** inside your application's `main/CMakeLists.txt`:

   ```cmake
   idf_component_register(
       SRCS "app.c"
       INCLUDE_DIRS "."
       REQUIRES usmp-esp32
   )
   ```

4. **Include the Headers** in your application code:

   ```c
   #include "usmp.h"
   #include "usmp_transport.h"
   ```

## Option 2: Arduino IDE (ESP32 Cores)

If you prefer building within the Arduino environment, USMP is packaged as a standard Arduino library.

### What You'll Need

* **Arduino IDE** (v2.0 or later) or PlatformIO.
* **ESP32 Arduino Core** (v2.0 or later) installed inside your IDE.

### Setup Instructions

=== "Import via ZIP (Recommended)"
    1. Find the pre-packaged zip archive (e.g., `usmp-0.4.7-arduino.zip`) in the root of your local repository copy.
    2. Open the Arduino IDE.
    3. Click on **Sketch** ➔ **Include Library** ➔ **Add .ZIP Library...**
    4. Select the zip file.

=== "Manual Copy"
    1. Copy the `ports/usmp-arduino/` folder.
    2. Paste it directly into your local Arduino libraries folder:
       ***Windows**: `Documents/Arduino/libraries/USMP`
       * **macOS / Linux**: `~/Arduino/libraries/USMP`
    3. Restart your Arduino IDE to trigger a refresh.

### Include the Library

In your sketch, include the main header file:

```cpp
#include <USMP.h>
```

## Option 3: Python SDK (Gateways & Tools)

The Python SDK is async-driven, making it perfect for writing gateways, backend integrations, or test scripts.

### What You'll Need

* **Python** (v3.11 or later).

### Setup Instructions

=== "Using pip"
    ```bash
    pip install usmp
    ```

=== "Using uv (Recommended)"
    ```bash
    uv add usmp
    ```

=== "From Source (Editable Mode)"
    If you want to edit or develop the Python SDK yourself:
    ```bash
    git clone https://github.com/metaloomlabs/usmp.git
    cd usmp
    uv add --editable sdk/python
    ```

## Configuring Your Credentials

### 1. The Pre-Shared Key (PSK)

For security, USMP does **not** support hardcoded compile-time keys. You must load and apply your PSK at runtime.

=== "ESP32 (ESP-IDF)"
    ```c
    usmp_t ctx = {0};

    // We recommend loading the PSK from secure NVS storage
    static const uint8_t secret_psk[] = "your-secret-key-here";
    ctx.psk     = secret_psk;
    ctx.psk_len = sizeof(secret_psk) - 1; // Exclude null terminator
    ```

=== "Arduino"
    ```cpp
    // Pass the PSK directly into the client constructor
    USMPClient usmp("your-secret-key-here");
    ```

=== "Python"
    ```python
    # Pass the key to the Server or Client instance
    PSK = b"your-secret-key-here"
    server = USMPServer(host="0.0.0.0", port=9000, psk=PSK)
    ```

> [!WARNING]
> Never deploy with default or public development keys in production.
> To generate a cryptographically strong random key for your production environment:
>
> ```bash
> python -c "import secrets; print(secrets.token_hex(32))"
> ```

### 2. Custom Port Bindings

USMP defaults to port `9000`. You can easily adjust this if port conflicts occur:

=== "ESP32 (ESP-IDF)"
    ```c
    usmp_transport_tcp_init(&transport, "192.168.1.100", 8888);
    ```

=== "Arduino"
    ```cpp
    // Pass custom port as the second parameter to USMP::TCP (default is 9000)
    usmp.begin(USMP::TCP("192.168.1.100", 8888).wifi("SSID", "PASS"));
    ```

=== "Python"
    ```python
    server = USMPServer(host="0.0.0.0", port=8888, psk=PSK)
    ```
