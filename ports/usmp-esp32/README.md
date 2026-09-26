# USMP — Unified Secure Multi-transport Protocol

[![ESP Component Registry](https://components.espressif.com/components/metaloomlabs/usmp/badge.svg)](https://components.espressif.com/components/metaloomlabs/usmp)

> ⚠️ **Note:** This repository is a read-only distribution mirror of the USMP monorepo.
> All development, pull requests, and issues should be submitted to [metaloomlabs/usmp](https://github.com/metaloomlabs/usmp).

Lightweight, mutually authenticated, AES-256-GCM / ChaCha20-Poly1305 encrypted communication for ESP32.  
No TLS stack. No certificates. Three function calls.

```bash
idf.py add-dependency "metaloomlabs/usmp"
```

## Why USMP

Full TLS is 60–100 KB of flash and significant RAM. Raw TCP has no security.  
USMP sits in between: a 4-message handshake gives you mutual authentication and a fresh session key, then every DATA frame is AEAD encrypted with replay protection. The entire C core has zero platform dependencies.

**Guarantees — no insecure mode:**

- Mutual authentication (HMAC-SHA256 + PSK, both sides verify)
- Forward secrecy (X25519 ephemeral key exchange per session)
- Encryption (AES-256-GCM or ChaCha20-Poly1305, mandatory)
- Replay protection (monotonic sequence numbers)

## Requirements

- ESP-IDF v5.0 or later
- Python counterpart: `pip install usmp` (the `USMPServer` / `USMPClient` gateway)

## Installation

**Via IDF Component Manager (recommended):**

```bash
idf.py add-dependency "metaloomlabs/usmp"
```

**Manual:** clone the repo and add `ports/usmp-esp32` as a component to your project's `components/` directory.

---

## Quickstart

### 1. Configure `sdkconfig` / `menuconfig`

No extra Kconfig entries required. USMP uses standard ESP-IDF `esp_wifi`, `lwip`, and `mbedtls` (only for AES-GCM and SHA-256 primitives — no TLS handshake).

*Note: The `metaloomlabs/usmp` component already provides the default implementations of platform hooks dynamically. You do not need to define them yourself. For reference (or custom ports), their correct signatures are:*

```c
#include "usmp_port.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>

static const uint8_t DEVICE_ID[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};

int usmp_port_get_device_id(uint8_t *out, size_t len) {
    if (len < 6) return -1;
    memcpy(out, DEVICE_ID, 6);
    return 0;
}

int usmp_port_random(uint8_t *buf, size_t len) {
    esp_fill_random(buf, len);
    return 0;
}

void usmp_port_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

uint32_t usmp_port_millis(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

void usmp_port_log(char level, const char *tag, const char *msg) {
    ESP_LOGI(tag, "%s", msg);
}
```

### 2. Connect and Communicate

```c
#include "usmp.h"
#include "usmp_transport.h"   // TCP/UDP transport

static const char PSK[] = "usmp-dev-psk-change-me-before-prod";

void app_main(void) {
    // ... WiFi connect ...

    usmp_t ctx = {0};
    usmp_transport_t transport = {0};

    // Set credentials and configuration on context
    ctx.psk = (const uint8_t *)PSK;
    ctx.psk_len = strlen(PSK);
    ctx.keepalive_ms = 15000; // PING every 15s when idle

    // Initialize TCP transport (supports IPs and DNS hostnames)
    if (usmp_transport_tcp_init(&transport, "192.168.1.100", 9000) != 0) {
        ESP_LOGE("APP", "Failed to connect TCP transport");
        return;
    }

    if (usmp_connect(&ctx, &transport) != 0) {
        ESP_LOGE("APP", "USMP handshake failed: %s", usmp_strerror(ctx.last_err));
        return;
    }

    ESP_LOGI("APP", "Connected to USMP server!");

    // Send payload
    uint8_t msg[] = "Hello from ESP32!";
    usmp_send(&ctx, msg, sizeof(msg) - 1);

    // Main execution loop
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100));

        // Receive incoming encrypted payload
        uint8_t buf[256];
        int n = usmp_recv(&ctx, buf, sizeof(buf));
        if (n > 0) {
            ESP_LOGI("APP", "Received %d bytes: %.*s", n, n, (char *)buf);
        }

        // Drive non-blocking state machine & background keepalives
        if (usmp_step(&ctx) != 0) {
            ESP_LOGW("APP", "Connection lost (%s)! Reconnecting...", usmp_strerror(ctx.last_err));
            while (usmp_reconnect(&ctx) != 0) {
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        }
    }
}
```

### 3. Zero-Heap Static Transports

For safety-critical or zero-heap embedded environments where dynamic memory (`malloc`) is forbidden, use the static transport initializers:

```c
// Statically allocated in .bss (zero heap fragmentation)
static usmp_tcp_ctx_t s_tcp_ctx;
usmp_transport_t transport = {0};

if (usmp_transport_tcp_init_static(&transport, &s_tcp_ctx, "192.168.1.100", 9000) != 0) {
    ESP_LOGE("APP", "Failed to connect static TCP transport");
    return;
}
```

Or for UDP:

```c
static usmp_udp_ctx_t s_udp_ctx;
usmp_transport_t transport = {0};

if (usmp_transport_udp_init_static(&transport, &s_udp_ctx, "192.168.1.100", 9000) != 0) {
    ESP_LOGE("APP", "Failed to connect static UDP transport");
    return;
}
```

### 4. Pair with Python Test Server

#### Option A: Zero-Code CLI Dev Server

```bash
pip install usmp
python -m usmp.server --echo --port 9000 --psk "usmp-dev-psk-change-me-before-prod"
```

#### Option B: Embedded Async Python Server

```python
import asyncio
from usmp import USMPServer, USMPSession, USMPProtocol, ConnectionClosedError

PSK = b"usmp-dev-psk-change-me-before-prod"
server = USMPServer(host="0.0.0.0", port=9000, psk=PSK, protocol=USMPProtocol.TCP)

@server.on_session
async def handle_device(session: USMPSession):
    print(f"Device connected: {session.device_id}")
    try:
        while session.is_connected:
            text = await session.recv_str()
            session.print(text)  # Formats timestamp and colored device ID badge
            await session.send(f"ACK: {text}")
    except ConnectionClosedError:
        print(f"Device disconnected: {session.device_id}")

async def main():
    await server.serve()

if __name__ == "__main__":
    asyncio.run(main())
```

---

## Handshake Overview

```text
Device                         Server
  │── HELLO (device_id, pub_C) ──►│
  │◄─ CHALLENGE (nonce, pub_S) ───│
  │── HELLO_ACK (HMAC) ──────────►│
  │◄─ SESSION_OK (session_id) ────│
         ↓  session established  ↓
  │══ DATA (AES-256-GCM) ════════►│
  │◄═ DATA (AES-256-GCM) ════════│
```

Session key derivation: `HKDF-SHA256(X25519(priv_C, pub_S), salt=nonce, info="usmp-v1"||pub_C||pub_S)`

## Packet Types

| Value | Name       | Direction         | Description                                        |
| :---- | :--------- | :---------------- | :------------------------------------------------- |
| 0x01  | HELLO      | Device → Server   | Handshake initiation with client public key        |
| 0x02  | CHALLENGE  | Server → Device   | Handshake challenge with server public key & nonce |
| 0x03  | HELLO_ACK  | Device → Server   | HMAC authentication response                       |
| 0x04  | SESSION_OK | Server → Device   | Mutual auth confirmed, session ID assigned         |
| 0x05  | DATA       | Both              | AEAD encrypted application payload                 |
| 0x06  | PING       | Both              | Liveness heartbeat                                 |
| 0x07  | PONG       | Both              | Liveness acknowledgment                            |
| 0x08  | BYE        | Both              | Clean session termination                          |
| 0x09  | REKEY      | Both              | Transparent in-band session key rotation           |
| 0xFF  | ERROR      | Both              | Diagnostic error frame                             |

## Transport Abstraction

`usmp_transport_t` is a struct of five function pointers (`send`, `recv`, `close`, `reconnect`, `available`). TCP and UDP over Wi-Fi are provided out-of-the-box with both dynamic (`init`) and zero-heap static (`init_static`) constructors. Custom transports (e.g. UART with COBS, BLE) can be easily integrated by implementing the interface.

## Roadmap

| Version | Feature                                                                                                              | Status   |
| :------ | :------------------------------------------------------------------------------------------------------------------- | :------- |
| v0.2.0  | Core protocol, Keepalive mechanism, and Arduino Port                                                                 | Released |
| v0.3.0  | TCP transport support and initial Python SDK                                                                         | Released |
| v0.4.0  | Published on ESP Component Registry and PyPI, making it stable                                                       | Released |
| v0.4.7  | Hardening (Deterministic Nonces, Rate Limiting, Dynamic Fragmentation)                                               | Released |
| v0.5.0  | UDP transport support fully complete and production-ready                                                            | Released |
| v1.1.0  | Hardening & security fixes, CLI tools reference                                                                      | Released |
| v1.2.0  | ChaCha20-Poly1305 cipher suite, In-Band Rekeying, Adaptive UDP RTT estimation                                        | Released |
| v1.3.0  | Zero-heap static transports, non-blocking handshake FSM, split RTOS concurrency, Arduino Print interface, Python dev | Released |
| v1.4.0  | Secure OTA firmware updates with Ed25519 signatures                                                                  | Planned  |

## License

Apache-2.0 — see [LICENSE](https://github.com/metaloomlabs/usmp/blob/main/LICENSE)
