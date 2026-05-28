# ESP32 Port

The ESP32 port implements the USMP platform hooks and TCP transport
for ESP-IDF v5.0+.

## Component structure

```

ports/usmp-esp32/
  port/
    usmp_port_esp32.c     ← platform hooks (MAC, RNG, delay, log)
  transport/
    usmp_transport_tcp.c  ← TCP transport implementation
  CMakeLists.txt
  idf_component.yml

```

## Platform hooks

The ESP32 port implements these functions from `usmp_port.h`:

| Function | ESP-IDF call |
|----------|-------------|
| `usmp_port_get_device_id` | `esp_read_mac(ESP_MAC_WIFI_STA)` |
| `usmp_port_random` | `esp_fill_random()` |
| `usmp_port_delay_ms` | `vTaskDelay(pdMS_TO_TICKS(ms))` |
| `usmp_port_millis` | `esp_timer_get_time() / 1000` |
| `usmp_port_log` | `ESP_LOGI/W/E` |

## TCP transport

The TCP transport uses lwIP sockets with `TCP_NODELAY` enabled
for lower handshake latency.

```c
// Initialize and connect
usmp_transport_t transport = {0};
if (usmp_transport_tcp_init(&transport, "192.168.1.100", 9000) != 0) {
    // connection failed
}

// Pass to usmp_connect
usmp_t ctx = {0};
usmp_connect(&ctx, &transport);
```

## Memory usage

| Component | RAM |
|-----------|-----|
| `usmp_t` context | ~60 bytes |
| TX/RX buffers | ~1KB (stack, during send/recv) |
| mbedtls ECDH (handshake only) | ~4KB (stack, freed after handshake) |

!!! tip "Stack size"
    The handshake allocates mbedtls contexts on the stack.
    Set `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192` in menuconfig
    or `sdkconfig.defaults`.

## Supported hardware

Any ESP32 variant with WiFi support:

- ESP32
- ESP32-S2
- ESP32-S3
- ESP32-C3
- ESP32-C6
