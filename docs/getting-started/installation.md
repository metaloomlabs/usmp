# Installation

## ESP32 (ESP-IDF)

### Requirements

- ESP-IDF v5.0 or later
- CMake 3.16 or later

### Add USMP to your project

Clone the repository:

```bash
git clone https://github.com/metaloomlabs/usmp.git
```

Add the ESP32 port as an extra component in your project's `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "/path/to/usmp/ports/usmp-esp32"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(your_project)
```

Add `usmp-esp32` to your `main/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "app.c"
    INCLUDE_DIRS "."
    REQUIRES usmp-esp32
)
```

Include the single public header:

```c
#include "usmp.h"
#include "usmp_transport.h"
```

## Arduino Library (ESP32)

### Requirements

- Arduino IDE v2.0 or later (or PlatformIO)
- ESP32 Arduino Core v2.0 or later installed in your IDE

### Installation

=== "Import ZIP Library"
    The repository includes a pre-packaged `usmp-*.zip` (e.g. `usmp-0.4.4-arduino.zip`) in the root directory.
    1. Open your Arduino IDE.
    2. Go to **Sketch** ➔ **Include Library** ➔ **Add .ZIP Library...**
    3. Choose the zip file from your local clone.

=== "Manual Installation"
    1. Clone the repository.
    2. Copy the `ports/usmp-arduino/` directory into your Arduino libraries folder (usually `Documents/Arduino/libraries/`).
    3. Rename the copied folder to `USMP`.
    4. Restart your Arduino IDE.

### Include the Library

```cpp
#include <USMP.h>
```

## Python SDK

### Requirements

- Python 3.11 or later

### Install via pip

```bash
pip install usmp
```

### Install via uv

```bash
uv add usmp
```

### Install from source

```bash
git clone https://github.com/metaloomlabs/usmp.git
cd usmp
uv add --editable sdk/python
```

## Configuration

### PSK (Pre-Shared Key)

The PSK must match on both sides. The default is for development only:

=== "ESP32 (ESP-IDF)"
    ```c
    // Set PSK dynamically at runtime in your application
    usmp_t ctx = {0};
    static const uint8_t s_psk[] = "your-secret-psk-here";
    ctx.psk     = s_psk;
    ctx.psk_len = sizeof(s_psk) - 1; // exclude null terminator
    ```

=== "Arduino"
    ```cpp
    // Passed directly into the client constructor
    USMPClient usmp("your-secret-psk-here");
    ```

=== "Python"
    ```python
    PSK = b"your-secret-psk-here"
    server = USMPServer(host="0.0.0.0", port=9000, psk=PSK)
    ```

!!! warning
    Never use the default PSK in production.
    Generate a random one:
    ```bash
    python3 -c "import secrets; print(secrets.token_hex(32))"
    ```

### Port and host

Default port is `9000`. Change it:

=== "ESP32 (ESP-IDF)"
    ```c
    usmp_transport_tcp_init(&transport, "192.168.1.100", 8888);
    ```

=== "Arduino"
    ```cpp
    // Pass port as the second parameter to USMP::TCP (default is 9000)
    usmp.begin(USMP::TCP("192.168.1.100", 8888).wifi("SSID", "PASS"));
    ```

=== "Python"
    ```python
    server = USMPServer(host="0.0.0.0", port=8888, psk=PSK)
    ```
