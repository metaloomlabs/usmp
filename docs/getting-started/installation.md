# Installation

## ESP32 (ESP-IDF)

### Requirements

- ESP-IDF v5.0 or later
- CMake 3.16 or later

### Add USMP to your project

Clone the repository:

```bash
git clone https://github.com/winterx64/usmp.git
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

## Python SDK

### Requirements

- Python 3.11 or later

### Install via pip

```bash
pip install usmp-python
```

### Install via uv

```bash
uv add usmp-python
```

### Install from source

```bash
git clone https://github.com/winterx64/usmp.git
cd usmp
uv add --editable sdk/python
```

## Configuration

### PSK (Pre-Shared Key)

The PSK must match on both sides. The default is for development only:

=== "ESP32"
    ```c
    // Define before including usmp.h
    #define USMP_PSK "your-secret-psk-here"
    #include "usmp.h"
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

=== "ESP32"
    ```c
    usmp_transport_tcp_init(&transport, "192.168.1.100", 8888);
    ```

=== "Python"
    ```python
    server = USMPServer(host="0.0.0.0", port=8888, psk=PSK)
    ```
