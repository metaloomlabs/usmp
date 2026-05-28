# Porting Guide

Porting USMP to a new platform requires implementing two things:

1. **Platform hooks** — 5 functions in `usmp_port_<platform>.c`
2. **Transport** — at minimum one transport (TCP or UART)

The USMP core (`core/`) never calls platform-specific functions directly.
All platform dependencies go through these interfaces.

---

## Step 1 — Implement platform hooks

Create `ports/<your-platform>/port/usmp_port_<platform>.c`
and implement these 5 functions:

```c
#include "usmp_port.h"

// Get unique device ID (MAC address, chip UID, etc.)
int usmp_port_get_device_id(uint8_t *out, size_t len)
{
    // Fill out with at least 6 bytes of unique ID
    // Return 0 on success, -1 on failure
}

// Cryptographically random bytes
int usmp_port_random(uint8_t *out, size_t len)
{
    // Fill out with len random bytes
    // Return 0 on success, -1 on failure
}

// Millisecond delay
void usmp_port_delay_ms(uint32_t ms)
{
    // Block for ms milliseconds
}

// Milliseconds since boot
uint32_t usmp_port_millis(void)
{
    // Return uptime in milliseconds
}

// Log output
void usmp_port_log(char level, const char *tag, const char *msg)
{
    // level: 'I' = info, 'W' = warn, 'E' = error
    // Print to serial, UART, semihosting, etc.
}
```

### Platform examples

=== "STM32 (HAL)"
    ```c
    int usmp_port_get_device_id(uint8_t *out, size_t len) {
        // STM32 96-bit unique ID at fixed address
        uint32_t*uid = (uint32_t *)0x1FFF7590;
        memcpy(out, uid, len < 12 ? len : 12);
        return 0;
    }

    int usmp_port_random(uint8_t *out, size_t len) {
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

=== "Arduino"
    ```cpp
    int usmp_port_get_device_id(uint8_t *out, size_t len) {
        uint64_t mac = ESP.getEfuseMac();
        memcpy(out, &mac, len < 6 ? len : 6);
        return 0;
    }

    int usmp_port_random(uint8_t *out, size_t len) {
        for (size_t i = 0; i < len; i++)
            out[i] = (uint8_t)esp_random();
        return 0;
    }

    void usmp_port_delay_ms(uint32_t ms) { delay(ms); }
    uint32_t usmp_port_millis(void) { return millis(); }
    ```

=== "Linux (for testing)"
    ```c
    #include <time.h>
    #include <fcntl.h>

    int usmp_port_get_device_id(uint8_t *out, size_t len) {
        // Use /etc/machine-id or a fixed test ID
        memset(out, 0xAB, len);
        return 0;
    }

    int usmp_port_random(uint8_t *out, size_t len) {
        int fd = open("/dev/urandom", O_RDONLY);
        read(fd, out, len);
        close(fd);
        return 0;
    }

    void usmp_port_delay_ms(uint32_t ms) {
        struct timespec ts = { ms / 1000, (ms % 1000) * 1000000 };
        nanosleep(&ts, NULL);
    }
    ```

---

## Step 2 — Implement a transport

Create `ports/<your-platform>/transport/usmp_transport_<type>.c`
and implement the transport interface:

```c
#include "usmp_transport.h"

static int my_send(usmp_transport_t *t, const uint8_t *data, size_t len)
{
    // Send exactly len bytes
    // Return 0 on success, -1 on failure
}

static int my_recv(usmp_transport_t *t, uint8_t *buf, size_t max_len)
{
    // Receive up to max_len bytes
    // Return number of bytes received, -1 on failure
}

static void my_close(usmp_transport_t *t)
{
    // Close the connection and free resources
}

// Factory function
int usmp_transport_my_init(usmp_transport_t *t, /* your params */)
{
    // Initialize your transport
    // Set up t->ctx with transport-specific state

    t->send  = my_send;
    t->recv  = my_recv;
    t->close = my_close;
    t->ctx   = /* your state */;
    return 0;
}
```

---

## Step 3 — Build system

=== "ESP-IDF"
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

=== "CMake"
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

=== "Arduino"
    ```
Copy core/src/* and core/include/* into your library folder.
    Add your port and transport files.
    Include usmp.h in your sketch.
    ```

---

## Checklist

- [ ] `usmp_port_get_device_id` returns at least 6 unique bytes
- [ ] `usmp_port_random` uses a hardware RNG or CSPRNG (not `rand()`)
- [ ] Transport `send` guarantees delivery of all bytes or returns -1
- [ ] Transport `recv` blocks until data is available or returns -1 on error
- [ ] Stack size is at least 8KB for the task running `usmp_connect`
- [ ] mbedtls is available and compiled with `MBEDTLS_HKDF_C=y`
