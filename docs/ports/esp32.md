# ESP-IDF Component Reference (ESP32)

Welcome, firmware engineers! The ESP32 port of USMP implements the core protocol's hardware-dependent platform hooks and provides high-performance, secure TCP and UDP transport adapters optimized for the native Espressif IoT Development Framework (ESP-IDF v5.0+).

---

## Component Installation

Since the USMP repository is private, you should add USMP directly via the Espressif Component Registry:

```bash
idf.py add-dependency "metaloomlabs/usmp"
```

This registers USMP as a project dependency. The ESP-IDF build system automatically downloads, configures, and links the component.

---

## Component Architecture

Once installed, the component exposes the following file structure:

* `port/`
  * `usmp_port_esp32.c` — Connects USMP's hardware hooks to Espressif's APIs (RNG, timers, logging, and Wi-Fi MAC reading).
* `transport/`
  * `usmp_transport_tcp.c` — Implements secure, non-blocking streams over lwIP TCP sockets.
  * `usmp_transport_udp.c` — Implements secure, low-overhead communication over lwIP UDP sockets with USMP reliability mechanisms.

---

## Platform Hook Mappings

USMP is designed to be completely platform-agnostic. The core state machine requests system services through hooks defined in `usmp_port.h`. On the ESP32, these map directly to Espressif's hardware abstractions:

| Hook Function | Under the Hood (ESP-IDF API) | Role |
| :--- | :--- | :--- |
| `usmp_port_get_device_id` | `esp_read_mac(out, ESP_MAC_WIFI_STA)` | Reads the factory-configured station Wi-Fi MAC address as the unique device ID. |
| `usmp_port_random` | `esp_fill_random(out, len)` | Employs the ESP32's hardware True Random Number Generator (TRNG) for cryptographic salts. |
| `usmp_port_delay_ms` | `vTaskDelay(pdMS_TO_TICKS(ms))` | Yields execution to FreeRTOS to prevent task starvation. |
| `usmp_port_millis` | `esp_timer_get_time() / 1000` | Retrieves system uptime millisecond counters. |
| `usmp_port_log` | `ESP_LOGI` / `ESP_LOGW` / `ESP_LOGE` | Routes internal protocol logs directly to the ESP-IDF terminal logging engine. |

---

## Transport Adaptors

### 1. TCP Transport

The TCP transport uses native BSD sockets provided by lwIP. To minimize handshake round-trip times and telemetry latency, we configure the socket with the `TCP_NODELAY` flag enabled.

```c
usmp_transport_t transport;
usmp_transport_tcp_init(&transport, "192.168.1.100", 9000);
```

### 2. UDP Transport

The UDP transport is connectionless and lightweight. It binds a local socket and routes packets to the target IP. USMP automatically handles sequence numbers, reliability, and frame validation.

```c
usmp_transport_t transport;
usmp_transport_udp_init(&transport, "192.168.1.100", 9000);
```

---

## Memory Footprint & Stack Allocation

USMP is optimized for resource-constrained environments. Once a session is active, it performs **zero heap allocations**!

| Context / Phase | RAM Consumption | Lifetime |
| :--- | :--- | :--- |
| **`usmp_t` Session Context** | ~112 bytes | Persistent (lives as long as the session is open). |
| **Transmit & Receive Buffers** | ~1 KB | Temporary stack memory (allocated only during send/recv calls). |
| **Handshake Buffers** | ~1 KB | Transient heap memory (freed immediately after handshake completes). |
| **mbedTLS Handshake Tasks** | ~2 KB – 4 KB | Transient stack memory (allocated during key exchange and signing). |

> [!IMPORTANT]
> **Task Stack Configurations**
> Because the cryptographic handshake performs Curve25519 calculations and HMAC signing, it allocates transient variables on the stack.
> To prevent stack overflows, ensure the task calling `usmp_connect()` has **at least 8 KB (8192 bytes)** of stack space allocated.
>
> If calling from the main task, increase the stack size in `sdkconfig`:
>
> ```ini
> CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
> ```
>
> If creating a custom FreeRTOS task:
>
> ```c
> xTaskCreate(usmp_task, "usmp_task", 8192, NULL, 5, NULL);
> ```

---

## Hardware Support

This component supports any Espressif silicon variant running ESP-IDF v5+:

* **ESP32** (Classic)
* **ESP32-S2** / **ESP32-S3**
* **ESP32-C3** / **ESP32-C6** / **ESP32-H2**
