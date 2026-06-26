# Porting Guide: Bringing USMP to New Hardware

Welcome! If you are looking to run USMP on a new microcontroller, RTOS, or custom hardware platform, you are in the right place.

One of USMP's core design values is **extreme portability**. The entire cryptographic state machine and framing engine live in a pure C core (`core/`) with **zero system dependencies**. It doesn't allocate memory on the heap, and it doesn't make direct OS calls.

To bring USMP to your target platform, you only need to build two things:

1. **Platform Hooks**: 5 simple system adapter functions.
2. **A Transport Adapter**: A wrapper that teaches USMP how to write and read bytes from your physical interfaces (TCP, UART, BLE, etc.).

Let's walk through the porting process step-by-step!

## Step 1: Implement Platform Hooks

First, create a new port file, e.g., `usmp_port_<myplatform>.c`, and implement the 5 system hooks declared in `usmp_port.h`:

```c
#include "usmp_port.h"

// 1. Retrieve the device's hardware identity (MAC address, unique chip ID, etc.)
int usmp_port_get_device_id(uint8_t *out, size_t len) {
    // Fill 'out' buffer with a unique hardware signature (at least 6 bytes).
    // Return 0 on success, or -1 on failure.
}

// 2. Obtain cryptographically secure random bytes
int usmp_port_random(uint8_t *out, size_t len) {
    // Fill 'out' with 'len' random bytes using a hardware TRNG or CSPRNG.
    // Return 0 on success, or -1 on failure.
}

// 3. Block execution for a set duration
void usmp_port_delay_ms(uint32_t ms) {
    // Suspend the active thread/task for 'ms' milliseconds.
}

// 4. Get system uptime
uint32_t usmp_port_millis(void) {
    // Return the number of milliseconds elapsed since system boot.
}

// 5. Route protocol logs
void usmp_port_log(char level, const char *tag, const char *msg) {
    // 'level' can be 'I' (Info), 'W' (Warning), or 'E' (Error).
    // Print this data to your serial output, console, or logging daemon.
}
```

### Example Implementations

Here is how these hooks look on various platforms:

=== "STM32 (HAL)"
    ```c
    int usmp_port_get_device_id(uint8_t *out, size_t len) {
        // STM32 96-bit unique chip ID is stored at a fixed address
        uint32_t*uid = (uint32_t *)0x1FFF7590;
        memcpy(out, uid, len < 12 ? len : 12);
        return 0;
    }

    int usmp_port_random(uint8_t *out, size_t len) {
        // Use STM32's hardware Random Number Generator
        for (size_t i = 0; i < len; i += 4) {
            uint32_t r;
            HAL_RNG_GenerateRandomNumber(&hrng, &r);
            memcpy(out + i, &r, len - i < 4 ? len - i : 4);
        }
        return 0;
    }

    void usmp_port_delay_ms(uint32_t ms) { HAL_Delay(ms); }
    uint32_t usmp_port_millis(void) { return HAL_GetTick(); }
    ```

=== "Arduino (ESP32)"
    ```cpp
    int usmp_port_get_device_id(uint8_t *out, size_t len) {
        uint64_t mac = ESP.getEfuseMac();
        memcpy(out, &mac, len < 6 ? len : 6);
        return 0;
    }

    int usmp_port_random(uint8_t *out, size_t len) {
        for (size_t i = 0; i < len; i++) {
            out[i] = (uint8_t)esp_random();
        }
        return 0;
    }

    void usmp_port_delay_ms(uint32_t ms) { delay(ms); }
    uint32_t usmp_port_millis(void) { return millis(); }
    ```

=== "Linux (Mock / POSIX Testing)"
    ```c
    #include <time.h>
    #include <fcntl.h>
    #include <unistd.h>

    int usmp_port_get_device_id(uint8_t *out, size_t len) {
        // Mock a fixed device ID for loopback testing
        memset(out, 0xAA, len);
        return 0;
    }

    int usmp_port_random(uint8_t *out, size_t len) {
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0) return -1;
        read(fd, out, len);
        close(fd);
        return 0;
    }

    void usmp_port_delay_ms(uint32_t ms) {
        struct timespec ts = { ms / 1000, (ms % 1000) * 1000000 };
        nanosleep(&ts, NULL);
    }
    ```

## Step 2: Implement a Transport Adapter

A transport adapter bridges USMP to your physical communication interface. You will need to implement a set of function pointers defined in `usmp_transport.h`:

```c
#include "usmp_transport.h"

// 1. Transmit bytes
static int my_send(usmp_transport_t *t, const uint8_t *data, size_t len) {
    // Write exactly 'len' bytes to the interface.
    // Return 0 on success, or -1 on failure.
}

// 2. Receive bytes
static int my_recv(usmp_transport_t *t, uint8_t *buf, size_t max_len) {
    // Read an incoming frame:
    // a. Read the 12-byte header first (USMP_HEADER_SIZE).
    // b. Parse the payload length field from the header.
    // c. Read the rest of the payload bytes.
    // Return total bytes read (12 + payload_len), or -1 on error/timeout.
}

// 3. Close the connection
static void my_close(usmp_transport_t *t) {
    // Release sockets, descriptors, and clean up local context.
}

// 4. Reconnect
static int my_reconnect(usmp_transport_t *t) {
    // Re-initialize the link and re-establish the connection.
    // Return 0 on success, or -1 on failure.
}

// 5. Query waiting bytes (Highly Recommended!)
static int my_available(usmp_transport_t *t) {
    // Return the number of bytes currently sitting in the receive buffer.
    // If you return 0 here, the core usmp_recv() knows it can return early 
    // instead of blocking during keepalive ticks.
    return 0; 
}

// Factory initialization function
int usmp_transport_my_init(usmp_transport_t *t, /* custom params */) {
    t->send      = my_send;
    t->recv      = my_recv;
    t->close     = my_close;
    t->reconnect = my_reconnect;
    t->available = my_available;
    t->ctx       = /* your custom driver context, e.g. socket fd */;
    return 0;
}
```

## Step 3: Configure Your Build System

=== "ESP-IDF (CMake)"
    Create a component directory and register your source files:
    ```cmake
    idf_component_register(
        SRCS
            "../../core/src/usmp_frame.c"
            "../../core/src/usmp_crypto.c"
            "../../core/src/usmp_handshake.c"
            "../../core/src/usmp_session.c"
            "../../core/src/usmp_connect.c"
            "port/usmp_port_myplatform.c"
            "transport/usmp_transport_tcp.c"
        INCLUDE_DIRS
            "../../core/include"
            "transport"
        PRIV_INCLUDE_DIRS
            "../../core/src"
        REQUIRES
            mbedtls
    )
    ```

=== "Standard CMake"
    ```cmake
    add_subdirectory(../../core)

    add_library(usmp-myplatform
        port/usmp_port_myplatform.c
        transport/usmp_transport_tcp.c
    )

    target_link_libraries(usmp-myplatform
        usmp-core
        mbedtls
    )
    ```

=== "Arduino IDE / PlatformIO"
    ```text
    1. Copy core/src/* and core/include/* directly into your library's folder.
    2. Add your platform hooks and transport sources.
    3. Include USMP.h in your main sketch.
    ```

## Porting Verification Checklist

Before you deploy your new port, make sure to check off these items:

* [ ] **Device ID Verification**: `usmp_port_get_device_id` returns at least 6 bytes of unique, non-zero identifiers.
* [ ] **Cryptographic Strength**: `usmp_port_random` uses hardware-seeded sources (never a pseudo-random generator like standard `rand()`).
* [ ] **Transport Delivery**: Transport `send` guarantees all bytes are pushed, or returns -1 on error.
* [ ] **Stack Safety**: Ensure the task driving the handshake has **at least 8 KB** of stack space allocated.
* [ ] **HKDF Capability**: Verify your cryptographic library is configured to compile with HKDF support enabled (e.g. `MBEDTLS_HKDF_C=y` in mbedTLS).
