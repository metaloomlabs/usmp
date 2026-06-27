# ESP-IDF Component Reference (ESP32)

Welcome, firmware engineers! The ESP32 port of USMP implements the core protocol's hardware-dependent platform hooks and sets up a high-performance TCP transport bridge using the native Espressif IoT Development Framework (ESP-IDF v5.0+).

## Component Architecture

The ESP32 port is structured as a standard ESP-IDF component that compiles directly alongside your application source code:

* `port/`
  * `usmp_port_esp32.c` — Connects USMP's hardware hooks to Espressif's APIs (RNG, timers, logging, and MAC reading).
* `transport/`
  * `usmp_transport_tcp.c` — Configures low-level lwIP BSD TCP sockets.
* `CMakeLists.txt` — Build configurations for the ESP-IDF component compiler.
* `idf_component.yml` — Component manager manifest.

## Platform Hook Mappings

USMP is platform-agnostic, meaning the core machine requests basic system services through a set of hooks defined in `usmp_port.h`. The ESP32 port maps these services directly to Espressif's hardware abstractions:

| Hook Function | Under the Hood (ESP-IDF API) | Role |
|:---|:---|:---|
| `usmp_port_get_device_id` | `esp_read_mac(out, ESP_MAC_WIFI_STA)` | Reads the factory-configured station Wi-Fi MAC address as the unique device ID. |
| `usmp_port_random` | `esp_fill_random(out, len)` | Employs the ESP32's hardware True Random Number Generator (TRNG) for cryptographic salts. |
| `usmp_port_delay_ms` | `vTaskDelay(pdMS_TO_TICKS(ms))` | Yields execution to FreeRTOS to prevent task starvation. |
| `usmp_port_millis` | `esp_timer_get_time() / 1000` | Retrieves system uptime millisecond counters. |
| `usmp_port_log` | `ESP_LOGI` / `ESP_LOGW` / `ESP_LOGE` | Routes internal protocol logs directly to the ESP-IDF terminal logging engine. |

## lwIP TCP Transport Optimization

The default TCP transport uses native BSD sockets provided by lwIP. To minimize handshake round-trip times and telemetry latency, we configure the socket with the `TCP_NODELAY` flag enabled:

```c
// 1. Initialize TCP transport
usmp_transport_t transport = {0};
if (usmp_transport_tcp_init(&transport, "192.168.1.100", 9000) != 0) {
    ESP_LOGE(TAG, "lwIP socket initialization failed!");
    return;
}

// 2. Perform secure connection
usmp_t ctx = {0};
ctx.psk = my_psk;
ctx.psk_len = sizeof(my_psk);

if (usmp_connect(&ctx, &transport) == 0) {
    ESP_LOGI(TAG, "Handshake complete over TCP socket.");
}
```

## Memory Footprint & Stack Allocation

USMP is designed from the ground up for embedded environments with strict memory constraints. It requires **zero heap allocations** once the session is established! (The handshake phase uses transient heap memory for negotiation buffers and mbedTLS contexts, which are completely freed before the handshake returns.)

| Context / Phase | RAM Consumption | Lifetime |
|:---|:---|:---|
| **`usmp_t` Session Context** | ~108 bytes | Persistent (lives as long as the session is open). |
| **Transmit & Receive Buffers** | ~1 KB | Temporary stack memory (allocated only during send/recv functions). |
| **Handshake Buffers (malloc)** | ~1 KB | Transient heap memory (allocated only during handshake, freed immediately). |
| **mbedTLS Handshake Tasks** | ~2 KB – 4 KB | Transient heap/stack memory (allocated during key exchange and freed immediately after). |

> [!IMPORTANT]
> **Task Stack Configurations**
> Because the cryptographic handshake performs Curve25519 calculations and HMAC signing, it allocates mbedTLS contexts on the running task's stack.
> To prevent stack overflows, ensure the task calling `usmp_connect()` has **at least 8 KB (8192 bytes)** of stack space allocated.
> If you are calling USMP from your main thread, you can increase the default stack limit inside your `sdkconfig` file:
>
> ```ini
> CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
> ```
>
> If calling from a custom FreeRTOS task, configure the stack parameter:
>
> ```c
> xTaskCreate(usmp_task, "usmp_task", 8192, NULL, 5, NULL);
> ```

## Hardware Support

This component is fully compatible with any Espressif silicon variant featuring standard Wi-Fi or Ethernet interfaces:

* **ESP32** (Classic)
* **ESP32-S2** / **ESP32-S3**
* **ESP32-C3** / **ESP32-C6**
