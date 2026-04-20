# Installation

## ESP32 (ESP-IDF)

### Requirements

- ESP-IDF v5.0 or later
- CMake 3.16 or later

### Add DXP to your project

Clone the repository:

```bash
git clone https://github.com/winterx64/dxp.git
```

Add the ESP32 port as an extra component in your project's `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "/path/to/dxp/ports/dxp-esp32"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(your_project)
```

Add `dxp-esp32` to your `main/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "app.c"
    INCLUDE_DIRS "."
    REQUIRES dxp-esp32
)
```

Include the single public header:

```c
#include "dxp.h"
#include "dxp_transport.h"
```

## Python SDK

### Requirements

- Python 3.11 or later

### Install via pip

```bash
pip install dxp-python
```

### Install via uv

```bash
uv add dxp-python
```

### Install from source

```bash
git clone https://github.com/winterx64/dxp.git
cd dxp
uv add --editable sdk/python
```

## Configuration

### PSK (Pre-Shared Key)

The PSK must match on both sides. The default is for development only:

=== "ESP32"
    ```c
    // Define before including dxp.h
    #define DXP_PSK "your-secret-psk-here"
    #include "dxp.h"
    ```

=== "Python"
    ```python
    PSK = b"your-secret-psk-here"
    server = DXPServer(host="0.0.0.0", port=9000, psk=PSK)
    ```

!!! warning
    Never use the default PSK in production.
    Generate a random one:
    ```bash
    python3 -c "import secrets; print(secrets.token_hex(32))"
    ```

### Port and host

Default port is `9000`. Change it:

=== "ESP32"
    ```c
    dxp_transport_tcp_init(&transport, "192.168.1.100", 8888);
    ```

=== "Python"
    ```python
    server = DXPServer(host="0.0.0.0", port=8888, psk=PSK)
    ```
