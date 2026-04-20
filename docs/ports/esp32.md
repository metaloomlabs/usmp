# ESP32 Port

The ESP32 port implements the DXP platform hooks and TCP transport
for ESP-IDF v5.0+.

## Component structure

```

ports/dxp-esp32/
  port/
    dxp_port_esp32.c     ← platform hooks (MAC, RNG, delay, log)
  transport/
    dxp_transport_tcp.c  ← TCP transport implementation
  CMakeLists.txt
  idf_component.yml

```

## Platform hooks

The ESP32 port implements these functions from `dxp_port.h`:

| Function | ESP-IDF call |
|----------|-------------|
| `dxp_port_get_device_id` | `esp_read_mac(ESP_MAC_WIFI_STA)` |
| `dxp_port_random` | `esp_fill_random()` |
| `dxp_port_delay_ms` | `vTaskDelay(pdMS_TO_TICKS(ms))` |
| `dxp_port_millis` | `esp_timer_get_time() / 1000` |
| `dxp_port_log` | `ESP_LOGI/W/E` |

## TCP transport

The TCP transport uses lwIP sockets with `TCP_NODELAY` enabled
for lower handshake latency.

```c
// Initialize and connect
dxp_transport_t transport = {0};
if (dxp_transport_tcp_init(&transport, "192.168.1.100", 9000) != 0) {
    // connection failed
}

// Pass to dxp_connect
dxp_t ctx = {0};
dxp_connect(&ctx, &transport);
```

## Memory usage

| Component | RAM |
|-----------|-----|
| `dxp_t` context | ~60 bytes |
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
